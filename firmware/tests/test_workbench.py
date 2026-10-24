import importlib.util,json,socket,tempfile,unittest
from pathlib import Path
from unittest.mock import patch

path=Path(__file__).resolve().parents[1]/'workbench.py'
spec=importlib.util.spec_from_file_location('car_workbench',path);workbench=importlib.util.module_from_spec(spec);spec.loader.exec_module(workbench)
class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        (path.parent.parent/'tmp').mkdir(exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=path.parent.parent/'tmp');self.file=Path(self.temp.name)/'device.json'
        self.file.write_text(json.dumps({'device_mac':'00-11-22-33-44-55','ip':'192.0.2.1'}));self.discovery=workbench.Discovery(self.file)
    def tearDown(self):self.temp.cleanup()
    def test_matching_identity_required(self):
        status={'type':'state','motor_control':True,'ip':'192.168.1.20','device_id':'001122334455'}
        with patch.object(workbench,'fetch',return_value=status):self.assertTrue(self.discovery.verify('192.168.1.20'))
        for field,value in [('device_id','another-car'),('ip','192.168.1.21'),('motor_control',False)]:
            with patch.object(workbench,'fetch',return_value={**status,field:value}):self.assertFalse(self.discovery.verify('192.168.1.20'))
    def test_new_ip_disconnects_old_channel_without_replaying_commands(self):
        self.discovery.publish('192.168.1.20');old,peer=socket.socketpair()
        try:
            self.discovery.channels.add(old);self.discovery.publish('192.168.2.30')
            self.assertEqual(peer.recv(1),b'');self.assertEqual(self.discovery.state()['ip'],'192.168.2.30')
            saved=json.loads(self.file.read_text());self.assertEqual(saved['ip'],'192.168.2.30')
            self.assertEqual(saved['workbench_url'],'http://127.0.0.1:8876/')
        finally:old.close();peer.close()
    def test_offline_state_keeps_fixed_workbench_identity(self):
        self.discovery.publish('192.168.1.20');self.discovery.publish(None)
        self.assertFalse(self.discovery.state()['online']);self.assertEqual(self.discovery.state()['device_id'],'001122334455')
if __name__=='__main__':unittest.main()
