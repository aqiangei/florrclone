"""Local native coordinator + two worker processes; NOT two GameServers."""
import concurrent.futures, json, multiprocessing, os, pathlib, socket, subprocess, sys, tempfile, time

def call(path, node, request, boot=None):
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(5);client.connect(path)
        client.sendall((json.dumps(dict(node=node,boot=boot or node+'-boot',request=request))+'\n').encode())
        chunks=[]
        while True:
            chunk=client.recv(65536)
            if not chunk:break
            chunks.append(chunk)
            if b'\n' in chunk:break
        if not chunks:raise EOFError('reply lost')
        return json.loads(b''.join(chunks))

def request(op, key, **kwargs):return dict(op=op,requestId=key,account='alice',**kwargs)
def mutate(op,key,lease,value,**kwargs):return request(op,key,epoch=lease['epoch'],revision=lease['revision'],record=dict(bag=value,totalXP=1000,loadout=['rose']),**kwargs)
def contender(args):
    path,node=args
    return node,call(path,node,request('acquire','parallel-login'))

class Service:
    def __init__(self,binary,root):self.binary=binary;self.root=pathlib.Path(root);self.socket=str(self.root/'authority.sock');self.proc=None
    def start(self,bootstrap=False,**faults):
        if pathlib.Path(self.socket).exists():pathlib.Path(self.socket).unlink()
        args=[self.binary,str(self.root/'authority.json'),self.socket]
        if bootstrap:args.append(str(self.root/'seed.json'))
        env=os.environ.copy();env.update(faults)
        self.proc=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env)
        line=self.proc.stdout.readline().strip()
        assert line=='READY local-only coordinator',(line,self.proc.stderr.read())
    def stop(self):
        if self.proc and self.proc.poll() is None:self.proc.terminate();self.proc.wait(timeout=6)
    def rpc(self,node,body):return call(self.socket,node,body)

def main(binary):
    with tempfile.TemporaryDirectory(prefix='florr-shard-fault-') as root:
        root=pathlib.Path(root);(root/'seed.json').write_text(json.dumps({'alice':{'bag':7,'totalXP':1000,'loadout':['rose']}}))
        svc=Service(binary,root)
        try:
            svc.start(bootstrap=True)
            # Separate OS workers contend on the same authority, not copied inventories.
            with concurrent.futures.ProcessPoolExecutor(max_workers=2) as pool:
                replies=list(pool.map(contender,[(svc.socket,'one'),(svc.socket,'two')]))
            winners=[(n,r) for n,r in replies if r['ok']];assert len(winners)==1,replies
            source,lease=winners[0];target='two' if source=='one' else 'one'
            pending=mutate('prepare','lost-response',lease,17,target=target,transfer='transfer-1')
            svc.stop();svc.start(SHARD_PROBE_DROP_REPLY_REQUEST='lost-response')
            try:svc.rpc(source,pending);raise AssertionError('expected reply drop')
            except EOFError:pass
            assert svc.proc.wait(timeout=5)==86
            svc.start()
            repeated=svc.rpc(source,pending);assert repeated['ok'] and repeated['replayed']
            assert not svc.rpc(source,mutate('checkpoint','old-source',lease,999))['ok']
            with concurrent.futures.ProcessPoolExecutor(max_workers=2) as pool:
                arrive=request('claim','claim-once',transfer='transfer-1')
                first,second=list(pool.map(claim_worker,[(svc.socket,target,arrive),(svc.socket,target,arrive)]))
            assert first['ok'] and second['ok'] and (first.get('replayed') or second.get('replayed'))
            assert first['record']['bag']==17
            lease=first
            back=mutate('prepare','return',lease,27,target=source,transfer='transfer-2')
            assert svc.rpc(target,back)['ok']
            home=svc.rpc(source,request('claim','home',transfer='transfer-2'));assert home['ok'] and home['record']['bag']==27
            # A rejected write cannot change the durable record or grant the other worker ownership.
            failing=mutate('prepare','disk-failure',home,37,target=target,transfer='transfer-3')
            svc.stop();svc.start(SHARD_PROBE_FAIL_REQUEST='disk-failure')
            assert svc.rpc(source,failing)['error']=='save_failed'
            assert not svc.rpc(target,request('claim','premature',transfer='transfer-3'))['ok']
            state=svc.rpc(source,request('inspect','read'));assert state['accounts']['alice']['record']['bag']==27
            svc.stop();svc.start();assert svc.rpc(source,failing)['ok']
            other=svc.rpc(target,request('claim','after-retry',transfer='transfer-3'));assert other['record']['bag']==37
            # Committed-on-disk but unknown-to-caller freezes until recovery, then replay resolves it.
            uncertain=mutate('checkpoint','unknown',other,47)
            svc.stop();svc.start(SHARD_PROBE_UNKNOWN_REQUEST='unknown')
            assert svc.rpc(target,uncertain)['error']=='commit_unknown'
            assert svc.rpc(source,request('acquire','cannot-steal'))['error']=='authority_frozen'
            svc.stop();svc.start();reply=svc.rpc(target,uncertain);assert reply['ok'] and reply['replayed']
            state=svc.rpc(target,request('inspect','final'));assert state['accounts']['alice']['record']['bag']==47
            assert state['accounts']['alice']['record']['loadout']==['rose']
            print('PASS: 2 OS worker processes; single owner, roundtrip, stale fences, duplicate claim, crash after durable prepare before reply, restart, failed write retry, ambiguous commit recovery.')
            print('NOT VALIDATED: real GameServer login/world ticks, economic acknowledgements, H rules, council API, remote latency or production performance.')
        finally:svc.stop()

def claim_worker(args):return call(*args)
if __name__=='__main__':main(sys.argv[1])
