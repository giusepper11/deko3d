import hashlib,io,json,os,subprocess,tarfile,tempfile,unittest
from pathlib import Path
import restore_workspace as restore

class RestoreTests(unittest.TestCase):
    def setUp(self):
        self.root=Path(tempfile.mkdtemp(prefix='migration-tests-',dir=os.environ['TMPDIR']))
    def tar(self,names):
        path=self.root/'test.tar'
        with tarfile.open(path,'w') as t:
            for name,kind in names:
                entry=tarfile.TarInfo(name);entry.size=2
                if kind=='link':entry.type=tarfile.SYMTYPE;entry.linkname='/outside';entry.size=0
                t.addfile(entry,io.BytesIO(b'ok') if entry.isfile() else None)
        return path
    def test_restore_new_only(self):
        archive=self.tar([('dusklight-deko3d/a', 'file')]);root=restore.extract(archive,self.root/'out')
        self.assertEqual((root/'a').read_bytes(),b'ok')
        with self.assertRaises(FileExistsError):restore.extract(archive,self.root/'out')
    def test_unsafe_paths_and_links_preflight_before_writes(self):
        for i,(name,kind) in enumerate([('/absolute','file'),('dusklight-deko3d/../escape','file'),('wrongroot/x','file'),('dusklight-deko3d/link','link')]):
            archive=self.tar([(name,kind)])
            with self.assertRaises(ValueError):restore.extract(archive,self.root/f'bad-{i}')
            self.assertFalse((self.root/f'bad-{i}').exists())
    def test_duplicate_archive_entry_rejected(self):
        archive=self.tar([('dusklight-deko3d/a','file')]*2)
        with self.assertRaises(ValueError):restore.extract(archive,self.root/'duplicate')
    def test_checksums_reject_corrupt_and_symlink(self):
        target=self.root/'workspace';target.mkdir();(target/'x').write_bytes(b'ab')
        manifest=self.root/'manifest.json';manifest.write_text(json.dumps({'files':[{'path':'x','bytes':2,'sha256':hashlib.sha256(b'ab').hexdigest()}]}))
        self.assertEqual(restore.verify(target,manifest),1);(target/'x').write_bytes(b'ac')
        with self.assertRaises(ValueError):restore.verify(target,manifest)
        (target/'x').unlink();(target/'x').symlink_to(manifest)
        with self.assertRaises(ValueError):restore.verify(target,manifest)
    def test_relocation_is_idempotent_even_nested_root(self):
        new=Path(restore.OLD_ROOT)/'migration/test-restore/dusklight-deko3d'
        text=restore.OLD_ROOT+'/source '+restore.OLD_ROOT+'/other'
        changed=restore.relocated(text,new)
        self.assertEqual(restore.relocated(changed,new),changed)
    def test_source_relocation_is_refused(self):
        with self.assertRaises(ValueError):restore.relocate(Path(restore.OLD_ROOT),self.root/'absent')
    def test_directory_permissions_survive_extraction_and_are_verified(self):
        archive=self.root/'private.tar'
        with tarfile.open(archive,'w') as tar:
            for name,mode in [('dusklight-deko3d',0o775),('dusklight-deko3d/private',0o700)]:
                entry=tarfile.TarInfo(name);entry.type=tarfile.DIRTYPE;entry.mode=mode;tar.addfile(entry)
        root=restore.extract(archive,self.root/'private-out')
        self.assertEqual((root/'private').stat().st_mode&0o777,0o700)
        manifest=self.root/'directories.json';manifest.write_text(json.dumps({'files':[],'directories':[{'path':'private','mode':0o700}]}))
        self.assertEqual(restore.verify(root,manifest),0)
        (root/'private').chmod(0o755)
        with self.assertRaises(ValueError):restore.verify(root,manifest)
    def test_unborn_repository_is_recorded_and_audited(self):
        from scan_workspace import main as scan_main
        import scan_workspace,sys
        workspace=self.root/'unborn-workspace';workspace.mkdir();repo=workspace/'unborn';repo.mkdir()
        subprocess.run(['git','init',str(repo)],check=True,capture_output=True)
        inventory=self.root/'unborn-inventory.json';old_root=scan_workspace.ROOT;old_argv=sys.argv
        scan_workspace.ROOT=workspace;sys.argv=['scan_workspace.py',str(inventory)]
        try:scan_main()
        finally:scan_workspace.ROOT=old_root;sys.argv=old_argv
        data=json.loads(inventory.read_text());self.assertEqual(len(data['repositories']),1)
        self.assertIsNone(data['repositories'][0]['head']);self.assertTrue(data['repositories'][0]['unborn'])
        self.assertEqual(len(restore.audit_git(workspace,inventory)),1)
    def test_real_worktree_and_submodule_relocation_preserves_dirty_state(self):
        original=self.root/'original';original.mkdir();main=original/'main';sub=original/'sub';work=original/'linked'
        env=os.environ|{'GIT_CONFIG_GLOBAL':'/dev/null','GIT_CONFIG_NOSYSTEM':'1'}
        def git(path,*args):
            return subprocess.check_output(['git','-c','user.name=Migration Test','-c','user.email=test@example.invalid','-c','core.hooksPath=/dev/null','-C',str(path),*args],env=env,stderr=subprocess.DEVNULL)
        for p in [main,sub]:
            p.mkdir();git(p,'init');(p/'file').write_text('base');git(p,'add','file');git(p,'commit','-m','base')
        git(main,'-c','protocol.file.allow=always','submodule','add',str(sub),'module');git(main,'commit','-am','module')
        git(main,'worktree','add','-b','linked',str(work));(work/'pending').write_text('unaccepted')
        repos=[]
        for p in [main,sub,main/'module',work]:
            repo={'path':str(p.relative_to(original)),'gitdir':git(p,'rev-parse','--absolute-git-dir').decode().strip().replace(str(original)+'/',''),
                  'common':git(p,'rev-parse','--path-format=absolute','--git-common-dir').decode().strip().replace(str(original)+'/','')}
            repo.update(restore.git_status(original,repo));repos.append(repo)
        inventory=self.root/'inventory.json';inventory.write_text(json.dumps({'repositories':repos}))
        import shutil
        destination=self.root/'relocated';shutil.copytree(original,destination)
        previous=restore.OLD_ROOT;restore.OLD_ROOT=str(original)
        try:
            restore.relocate(destination,inventory);self.assertEqual(len(restore.audit_git(destination,inventory)),4)
            self.assertEqual(restore.relocate(destination,inventory),[])
            self.assertEqual((original/'linked/pending').read_text(),'unaccepted')
        finally:restore.OLD_ROOT=previous

if __name__=='__main__':unittest.main(verbosity=2)
