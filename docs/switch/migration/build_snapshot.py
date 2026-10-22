"""Private offline snapshot, not a publication or acceptance of dirty changes."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,tarfile
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from scan_workspace import ROOT,PATTERNS,git
IMAGE='sha256:6611f256a5f9c97ff9b934c7a7c0ec26552aee0fe94d4cd59d849def637e376c'

def digest(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def record(p):return {'file':p.name,'bytes':p.stat().st_size,'sha256':digest(p)}
def main():
    os.umask(0o077)
    package=Path(sys.argv[1]).resolve();assert package.is_relative_to(ROOT/'migration')
    package.mkdir(parents=True,exist_ok=True)
    package.chmod(0o700)
    inventory=package/'repositories.json';assert not inventory.exists()
    subprocess.run([sys.executable,str(HERE/'scan_workspace.py'),str(inventory)],check=True,stdout=(package/'scan-summary.log').open('x'))
    data=json.loads(inventory.read_text());unresolved=[]
    for item in data['sensitive_candidates']:
        if item['reason']=='credential_filename':unresolved.append(item);continue
        matches=[(i,m) for i,pattern in enumerate(PATTERNS) for m in re.finditer(pattern,(ROOT/item['path']).read_bytes())]
        placeholders=item['path'].endswith('/spirv-tools/src/.github/workflows/autoroll.yml') and matches and all(
            i==1 and m.group().split(b'://',1)[1].split(b':',1)[1].startswith(b'$') for i,m in matches)
        if not placeholders:unresolved.append(item)
    assert not unresolved,[(x['path'],x['reason']) for x in unresolved]
    assert not data['special_files'] and not data['symlinks']
    (package/'privacy-scope.json').write_text(json.dumps({'automatic_candidates':data['sensitive_candidates'],'all_candidates_classified':'shell variable placeholders, not literal credentials','host_credentials_included':False,'scope':'Only project workspace, never ~/.hermes, SSH keys, Docker login config or home config. Not a complete secrets audit of Git history/binary logs. Treat all output as private.'},indent=2)+'\n')
    overlays=package/'pending-overlays';overlays.mkdir(exist_ok=False)
    pending=[]
    for index,repo in enumerate(data['repositories']):
        if not repo['status']:continue
        path=ROOT/repo['path'];dest=overlays/f'{index:03d}';dest.mkdir()
        for name,command in [('staged.patch',['diff','--cached','--binary']),('unstaged.patch',['diff','--binary'])]:
            r=git(path,*command);assert r.returncode==0;(dest/name).write_bytes(r.stdout)
        r=git(path,'ls-files','--others','--exclude-standard','-z');assert r.returncode==0
        files=[os.fsdecode(x) for x in r.stdout.split(b'\0') if x]
        if files:
            with tarfile.open(dest/'untracked.tar','x:') as tar:
                for relative in files:tar.add(path/relative,arcname=relative,recursive=False)
        pending.append({'repository':repo['path'],'head':repo['head'],'status':repo['status'],'overlay_dir':str(dest.relative_to(package)),'label':'PENDING/UNACCEPTED snapshot; not a validated correction or new hardware evidence'})
    (package/'pending-changes.json').write_text(json.dumps(pending,indent=2)+'\n')
    unreadable=[]
    for current,dirs,files in os.walk(ROOT):
        if Path(current)==ROOT:dirs[:]=[d for d in dirs if d!='migration']
        unreadable.extend(str((Path(current)/name).relative_to(ROOT)) for name in files if not os.access(Path(current)/name,os.R_OK))
    overrides={}
    if unreadable:
        raw=package/'permission-readback.tar'
        command=['docker','run','--name','dusklight-migration-permission-readback','--user','0:0','--network','none','--read-only','--mount',f'type=bind,src={ROOT},dst=/source,readonly','-i',IMAGE,'tar','--create','--file=-','--directory=/source','--null','--verbatim-files-from','--no-recursion','--files-from=-']
        with raw.open('xb') as out:
            subprocess.run(command,input=b'\0'.join(os.fsencode(p) for p in unreadable)+b'\0',stdout=out,check=True)
        copydir=package/'permission-readback';copydir.mkdir()
        with tarfile.open(raw) as tar:
            for member in tar:
                assert member.name in unreadable and member.isfile(),member.name
                destination=copydir/member.name;destination.parent.mkdir(parents=True,exist_ok=True)
                payload=tar.extractfile(member);assert payload is not None
                with destination.open('xb') as out:shutil.copyfileobj(payload,out)
                overrides[member.name]=destination
        assert set(overrides)==set(unreadable)
        (package/'permission-readback.json').write_text(json.dumps({'source_mount':'read-only','source_owner_mode_unchanged':True,'files':unreadable,'command':command},indent=2)+'\n')
    entries=[];paths=[];directories=[]
    for current,dirs,files in os.walk(ROOT,followlinks=False):
        p=Path(current)
        if p==ROOT:dirs[:]=[d for d in dirs if d!='migration']
        dirs.sort()
        directories.append({'path':str(p.relative_to(ROOT)),'mode':p.stat().st_mode&0o777})
        paths.append(os.fsencode(ROOT.name if p==ROOT else ROOT.name+'/'+str(p.relative_to(ROOT))))
        for name in sorted(files):
            f=p/name
            if f.is_symlink() or not f.is_file():raise ValueError('unsupported snapshot file: '+str(f))
            before=f.stat();h=digest(overrides.get(str(f.relative_to(ROOT)),f));after=f.stat()
            assert (before.st_size,before.st_mtime_ns)==(after.st_size,after.st_mtime_ns),('source changed',f)
            relative=f.relative_to(ROOT)
            entries.append({'path':str(relative),'bytes':after.st_size,'sha256':h,'mode':after.st_mode&0o777})
            paths.append(os.fsencode(ROOT.name+'/'+str(relative)))
    manifest={'original_root':str(ROOT),'file_count':len(entries),'total_regular_bytes':sum(x['bytes'] for x in entries),'exclusions':['migration/ (package outputs, tests and restore copies only)'],'files':entries,'directories':directories}
    (package/'files.json').write_text(json.dumps(manifest,separators=(',',':'))+'\n')
    listfile=package/'snapshot-paths.nul';listfile.write_bytes(b'\0'.join(paths)+b'\0')
    workspace=package/'workspace.tar.zst';assert not workspace.exists()
    print('SNAPSHOT',len(entries),'files',manifest['total_regular_bytes'],'bytes',flush=True)
    with workspace.open('xb') as out,(package/'workspace-tar.log').open('x') as errors:
        compressor=subprocess.Popen(['zstd','-T2','-3','-q'],stdin=subprocess.PIPE,stdout=out,stderr=errors)
        assert compressor.stdin is not None
        with tarfile.open(fileobj=compressor.stdin,mode='w|') as tar:
            for relative in paths:
                name=os.fsdecode(relative);source=ROOT.parent/name
                info=tar.gettarinfo(str(source),arcname=name)
                if source.is_dir():tar.addfile(info);continue
                # Emit every file independently; no archive links or dependency on
                # inaccessible host permissions. Metadata still comes from source.
                info.type=tarfile.REGTYPE;info.linkname='';info.size=source.stat().st_size
                with overrides.get(str(source.relative_to(ROOT)),source).open('rb') as payload:tar.addfile(info,payload)
        compressor.stdin.close();code=compressor.wait();assert code==0,('zstd',code)
    print('WORKSPACE_ARCHIVE',workspace.stat().st_size,flush=True)
    image=package/'toolchain.docker.tar.zst';assert not image.exists()
    with image.open('xb') as out,(package/'docker-save.log').open('x') as errors:
        save=subprocess.Popen(['docker','save','dusklight-mesa-upstream:0ff5207-toolchain'],stdout=subprocess.PIPE,stderr=errors)
        assert save.stdout is not None
        zstd=subprocess.run(['zstd','-T2','-3','-q'],stdin=save.stdout,stdout=out,stderr=errors);save.stdout.close()
        code=save.wait();assert code==zstd.returncode==0,('docker save/zstd',code,zstd.returncode)
    docker=json.loads(subprocess.check_output(['docker','image','inspect',IMAGE],text=True))
    # Store identity/architecture only; do not export daemon or account configuration.
    meta={key:docker[0][key] for key in ['Id','RepoTags','Architecture','Os','Size','RootFS']}
    (package/'toolchain.json').write_text(json.dumps(meta,indent=2)+'\n')
    for name in ['restore_workspace.py','test_restore.py','scan_workspace.py','build_snapshot.py']:
        shutil.copyfile(HERE/name,package/name)
    receipt={'workspace':record(workspace),'toolchain':record(image),'manifest':record(package/'files.json'),'inventory':record(inventory),'source_revision':git(HERE,'rev-parse','HEAD').stdout.decode().strip(),'file_count':len(entries),'total_regular_bytes':manifest['total_regular_bytes'],'pending_repositories':len(pending),'image':IMAGE,'private_only':True,'hardware_approved':False}
    (package/'package.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2),flush=True)
if __name__=='__main__':main()
