"""Offline restore and checksum verification. Never contacts a console or network."""
import argparse,hashlib,json,os,stat,subprocess,tarfile
from pathlib import Path,PurePosixPath
OLD_ROOT='/home/gro/dusklight-deko3d'

def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def safe_relative(name):
    p=PurePosixPath(name)
    if not name or name.startswith('/') or any(x in ('','..') for x in name.split('/')) or p.parts[0] not in ['dusklight-deko3d']:
        raise ValueError('unsafe archive path: '+name)
    return p

def extract(archive,destination):
    destination=Path(destination)
    if destination.exists():raise FileExistsError('destination must not exist: '+str(destination))
    # Complete validation before writing anything. This private snapshot deliberately
    # rejects links/special files rather than attempting a general tar extractor.
    with tarfile.open(archive,'r:') as tar:
        seen=set()
        for member in tar:
            safe_relative(member.name)
            if member.name in seen or not (member.isfile() or member.isdir()):raise ValueError('duplicate/link/special member: '+member.name)
            seen.add(member.name)
    destination.mkdir(mode=0o700,parents=False,exist_ok=False)
    directory_modes=[]
    with tarfile.open(archive,'r:') as tar:
        for member in tar:
            relative=safe_relative(member.name)
            output=destination.joinpath(*relative.parts)
            if member.isdir():
                output.mkdir(parents=True,exist_ok=True);directory_modes.append((output,member.mode&0o777));continue
            output.parent.mkdir(parents=True,exist_ok=True)
            with output.open('xb') as f:
                source=tar.extractfile(member)
                if source is None:raise ValueError('missing regular-file payload')
                while chunk:=source.read(1024*1024):f.write(chunk)
            output.chmod(member.mode&0o777)
    for directory,mode in sorted(directory_modes,key=lambda pair:len(pair[0].parts),reverse=True):directory.chmod(mode)
    return destination/'dusklight-deko3d'

def verify(root,manifest):
    root=Path(root).resolve();data=json.loads(Path(manifest).read_text())
    if not root.is_dir():raise FileNotFoundError(root)
    for entry in data['files']:
        relative=safe_relative('dusklight-deko3d/'+entry['path'])
        p=root.joinpath(*relative.parts[1:])
        if p.is_symlink() or not p.is_file() or p.stat().st_size!=entry['bytes'] or digest(p)!=entry['sha256'] or ('mode' in entry and p.stat().st_mode&0o777!=entry['mode']):
            raise ValueError('checksum/type mismatch: '+entry['path'])
    for entry in data.get('directories',[]):
        p=root if entry['path']=='.' else root.joinpath(*safe_relative('dusklight-deko3d/'+entry['path']).parts[1:])
        if p.is_symlink() or not p.is_dir() or p.stat().st_mode&0o777!=entry['mode']:raise ValueError('directory mode/type mismatch: '+entry['path'])
    return len(data['files'])

def relocated(text,new_root):
    # Only metadata paths, never source/build/evidence strings. An idempotent
    # rewrite: do not replace old-root-looking substrings already inside new_root.
    if str(new_root)==OLD_ROOT:return text
    token='\x00MIGRATION_NEW_ROOT\x00'
    if token in text:raise ValueError('unexpected metadata token')
    return text.replace(str(new_root),token).replace(OLD_ROOT,str(new_root)).replace(token,str(new_root))

def relocate(root,inventory):
    root=Path(root).resolve()
    if str(root)==OLD_ROOT:raise ValueError('refuse changes to original workspace')

    data=json.loads(Path(inventory).read_text());changed=[]
    for repo in data['repositories']:
        gd=root/repo['gitdir'];common=root/repo['common']
        for candidate in [root/repo['path']/'.git',gd/'gitdir',gd/'commondir',gd/'config',gd/'config.worktree',common/'config',common/'config.worktree',common/'objects/info/alternates']:
            if not candidate.is_file() or candidate.is_symlink():continue
            text=candidate.read_text();new=relocated(text,root)
            if text!=new:
                # Backup exclusivity: never overwrite a previous migration backup.
                backup=root/'.migration-metadata-originals'/candidate.relative_to(root)
                if backup.exists():raise FileExistsError(backup)
                backup.parent.mkdir(parents=True,exist_ok=True)
                backup.write_text(text);candidate.write_text(new);changed.append(str(candidate.relative_to(root)))
    return changed

def git_status(root,repo):
    env={k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
    env.update(GIT_OPTIONAL_LOCKS='0',GIT_TERMINAL_PROMPT='0',GIT_NO_LAZY_FETCH='1',GIT_CONFIG_NOSYSTEM='1',GIT_CONFIG_GLOBAL='/dev/null')
    def run(*args):
        r=subprocess.run(['git','-c','core.hooksPath=/dev/null','-c','core.fsmonitor=false','-c','protocol.allow=never','-C',str(root/repo['path']),*args],env=env,capture_output=True,check=True)
        return r.stdout.decode(errors='surrogateescape')
    head_result=subprocess.run(['git','-c','core.hooksPath=/dev/null','-c','core.fsmonitor=false','-c','protocol.allow=never','-C',str(root/repo['path']),'rev-parse','--verify','--quiet','HEAD'],env=env,capture_output=True)
    if head_result.returncode not in (0,1):raise ValueError('cannot verify HEAD: '+repo['path'])
    head=head_result.stdout.decode().strip() if head_result.returncode==0 else None
    return {'head':head,'branch':run('branch','--show-current').strip(),
            'status':run('status','--porcelain=v1','--untracked-files=all','--ignore-submodules=none'),
            'hidden_index_flags':[x for x in run('ls-files','-v').splitlines() if x[0].islower() or x.startswith('S ')],
            'submodules':run('submodule','status','--recursive')}

def audit_git(root,inventory):
    root=Path(root).resolve();data=json.loads(Path(inventory).read_text());results=[]
    for repo in data['repositories']:
        env={k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
        env.update(GIT_OPTIONAL_LOCKS='0',GIT_TERMINAL_PROMPT='0',GIT_NO_LAZY_FETCH='1',GIT_CONFIG_NOSYSTEM='1',GIT_CONFIG_GLOBAL='/dev/null')
        prefix=['git','-c','core.hooksPath=/dev/null','-c','core.fsmonitor=false','-c','protocol.allow=never','-C',str(root/repo['path'])]
        for args,expected in [(['rev-parse','--absolute-git-dir'],root/repo['gitdir']),(['rev-parse','--path-format=absolute','--git-common-dir'],root/repo['common']),(['rev-parse','--show-toplevel'],root/repo['path'])]:
            resolved=Path(subprocess.check_output(prefix+args,env=env,text=True).strip()).resolve()
            if resolved!=expected.resolve() or not resolved.is_relative_to(root):raise ValueError('Git resolves outside expected restored metadata: '+repo['path'])
        actual=git_status(root,repo)
        for key in actual:
            if actual[key]!=repo[key]:raise ValueError('Git state mismatch: '+repo['path']+' '+key)
        for field in ['gitdir','common']:
            if not (root/repo[field]).resolve().is_relative_to(root):raise ValueError('external Git metadata')
        results.append(repo['path'])
    return results

def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('extract');p.add_argument('archive');p.add_argument('new_destination')
    p=sub.add_parser('verify');p.add_argument('root');p.add_argument('manifest')
    p=sub.add_parser('relocate');p.add_argument('root');p.add_argument('inventory')
    p=sub.add_parser('audit-git');p.add_argument('root');p.add_argument('inventory')
    args=parser.parse_args()
    if args.command=='extract':print(extract(args.archive,args.new_destination))
    elif args.command=='verify':print('files verified:',verify(args.root,args.manifest))
    elif args.command=='relocate':print(json.dumps({'metadata_paths_changed':relocate(args.root,args.inventory)},indent=2))
    else:print('repositories verified:',len(audit_git(args.root,args.inventory)))
if __name__=='__main__':main()
