"""Read-only migration discovery; never print matching credential content."""
from pathlib import Path
import argparse,json,os,re,subprocess
ROOT=Path('/home/gro/dusklight-deko3d')
EXCLUDED_DIRS={'migration'}
PATTERNS=[rb'-----BEGIN (?:RSA |OPENSSH |EC |DSA )?PRIVATE KEY-----',
          rb'(?i)(?:ftp|https?)://[^\s/@:]+:[^\s/@]+@',
          rb'(?i)(?:--password|--passwd|--ftp-password)\s+[\'\"]?[^\s\'\"]+',
          rb'(?i)(?:Authorization[\"\']?\s*:\s*[\"\']?(?:Bearer|Basic))\s+[A-Za-z0-9+/=_-]{16,}',
          rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})']

def git(p,*args):
    env={k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
    env.update(GIT_OPTIONAL_LOCKS='0',GIT_TERMINAL_PROMPT='0',GIT_NO_LAZY_FETCH='1',GIT_CONFIG_NOSYSTEM='1',GIT_CONFIG_GLOBAL='/dev/null')
    r=subprocess.run(['git','-c','core.hooksPath=/dev/null','-c','core.fsmonitor=false','-c','protocol.allow=never','-C',str(p),*args],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    return r

def main():
    a=argparse.ArgumentParser();a.add_argument('output');args=a.parse_args()
    repositories=[];symlinks=[];sensitive=[];git_metadata=[];special=[]
    for current,dirs,files in os.walk(ROOT,followlinks=False):
        p=Path(current);rel=p.relative_to(ROOT)
        if not rel.parts:dirs[:]=[d for d in dirs if d not in EXCLUDED_DIRS]
        if '.git' in dirs or '.git' in files:
            head=git(p,'rev-parse','HEAD')
            if git(p,'rev-parse','--is-inside-work-tree').returncode==0:
                def g(*items):
                    r=git(p,*items);assert r.returncode==0,(str(rel),items,r.stderr.decode(errors='replace'));return r.stdout.decode(errors='surrogateescape')
                common=Path(g('rev-parse','--path-format=absolute','--git-common-dir').strip())
                gd=Path(g('rev-parse','--absolute-git-dir').strip())
                assert common.is_relative_to(ROOT) and gd.is_relative_to(ROOT),(rel,'external_git_storage')
                alternate=common/'objects/info/alternates'
                assert not alternate.exists() or not alternate.read_text().strip(),(rel,'external_alternates')
                d={'path':str(rel),'head':head.stdout.decode().strip() if head.returncode==0 else None,'unborn':head.returncode!=0,'branch':g('branch','--show-current').strip(),
                   'gitdir':str(gd.relative_to(ROOT)),'common':str(common.relative_to(ROOT)),
                   'shallow':g('rev-parse','--is-shallow-repository').strip()=='true',
                   'status':g('status','--porcelain=v1','--untracked-files=all','--ignore-submodules=none'),
                   'hidden_index_flags':[l for l in g('ls-files','-v').splitlines() if l[0].islower() or l.startswith('S ')],
                   'submodules':g('submodule','status','--recursive')}
                repositories.append(d)
        for name in dirs[:]:
            f=p/name
            if f.is_symlink():
                symlinks.append({'path':str(f.relative_to(ROOT)),'target':os.readlink(f)});dirs.remove(name)
        if '.git' in dirs: dirs.remove('.git')
        for name in files:
            f=p/name;r=str(f.relative_to(ROOT))
            if f.is_symlink():symlinks.append({'path':r,'target':os.readlink(f)});continue
            if not f.is_file():special.append(r);continue
            if name in ['.env','.netrc','.npmrc','.pypirc','id_rsa','id_ed25519','credentials.json'] or (name.startswith('.env.') and name!='.env.example'):
                sensitive.append({'path':r,'reason':'credential_filename'});continue
            # Inspect likely human logs/config only; do not scan object databases or
            # arbitrary binary payloads. Findings report filenames, never values.
            if name=='.git' or name.endswith(('.log','.json','.yaml','.yml','.txt','.md','.sh','.py','.cmake')) or 'log' in name:
                if f.stat().st_size>16*1024*1024:continue
                data=f.read_bytes()
                if b'\0' in data[:8192]:continue
                if any(re.search(pattern,data) for pattern in PATTERNS):sensitive.append({'path':r,'reason':'credential_pattern_requires_private_handling'})
    for repo in repositories:
        common=ROOT/repo['common']
        if str(common.relative_to(ROOT)) in git_metadata:continue
        git_metadata.append(str(common.relative_to(ROOT)))
    out={'original_root':str(ROOT),'repositories':repositories,'symlinks':symlinks,'sensitive_candidates':sensitive,
         'git_metadata':git_metadata,'special_files':special,'credential_scan_scope':'selected human-readable files <=16MiB only; not a complete secret/history audit'}
    dest=Path(args.output);assert not dest.exists();dest.write_text(json.dumps(out,indent=2)+'\n')
    print(json.dumps({'repositories':len(repositories),'shallow':sum(r['shallow'] for r in repositories),
                      'dirty_repositories':[(r['path'],r['status']) for r in repositories if r['status'] and '/build/' not in r['path']],
                      'sensitive_candidates':sensitive,'symlinks':len(symlinks),'special_files':special},indent=2))
if __name__=='__main__':main()
