import json, subprocess, tempfile, pathlib, sys, time

binary=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='hyper-barrier-') as work:
    folder=pathlib.Path(work)
    seed={'users':{},'players':{
        'a':{'inventory':{'unique':{'petal_rose':20}}},
        'b':{'inventory':{'apex':{'petal_rose':1},'unique':{'petal_rose':2}}}},
        'hyperForges':{'rose':{'owner':'b','claims':1,'paid':5}}}
    (folder/'seed.json').write_text(json.dumps(seed))
    def start():
        return subprocess.Popen([binary,str(folder/'authority'),str(folder/'seed.json')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    process=start();serial=0
    def call(node,op,**fields):
        global serial
        serial+=1
        command={'op':op,'requestId':f'req-{serial}',**fields}
        process.stdin.write(json.dumps({'id':str(serial),'node':node,'boot':'boot-'+node,'request':command})+'\n');process.stdin.flush()
        result=json.loads(process.stdout.readline())['result']
        return result,command
    def yes(node,op,**fields):
        result,_=call(node,op,**fields)
        assert result.get('ok'),result
        return result
    def fence(a):return {k:a[k] for k in ('epoch','revision')}
    a=yes('one','acquire',account='a');b=yes('two','acquire',account='b')
    tx=yes('one','hyper_begin',account='a',action='forge',input='rose')['transaction']['id']
    assert not call('two','acquire',account='a')[0]['ok']
    assert not call('one','coordinated_patch',accounts={},patch={})[0]['ok']
    # Final gameplay progress is saved before ready, and then fenced even from
    # the original game until it has installed the settled record.
    a=yes('one','checkpoint',account='a',record={'inventory':{'unique':{'petal_rose':20}},'totalXp':12345},**fence(a))
    yes('one','hyper_ready',transaction=tx,account='a',**fence(a))
    assert not call('one','checkpoint',account='a',record={},**fence(a))[0]['ok']
    assert call('one','hyper_progress',transaction=tx)[0]['error']=='hyper_waiting'
    yes('two','hyper_ready',transaction=tx,account='b',**fence(b))
    settlement,command=call('one','hyper_progress',transaction=tx)
    assert settlement['ok'] and settlement['transaction']['result']['ok'],settlement
    status=yes('one','hyper_status');after=status['accounts']['a']
    assert after['record']['totalXp']==12345
    assert after['record']['inventory']['unique']['petal_rose']==15
    assert after['record']['inventory']['apex']['petal_rose']==1
    assert status['hyperForges']['rose']['owner']=='a'
    loser=yes('two','hyper_status')['accounts']['b']
    assert loser['record']['inventory']['unique']['petal_rose']==6
    assert loser['record']['inventory'].get('apex',{}).get('petal_rose',0)==0
    assert not call('two','hyper_thaw',transaction=tx,account='a',**fence(after))[0]['ok']
    # Restart after fsync but before either game has seen a result. The same
    # original request is replayed, never charged a second time.
    process.stdin.close();assert process.wait(timeout=10)==0
    process=start();serial+=1
    process.stdin.write(json.dumps({'id':str(serial),'node':'one','boot':'boot-one','request':command})+'\n');process.stdin.flush()
    replay=json.loads(process.stdout.readline())['result'];assert replay['ok'] and replay['replayed']
    assert yes('one','hyper_status')['accounts']['a']['record']==after['record']
    yes('one','hyper_thaw',transaction=tx,account='a',**fence(after))
    yes('two','hyper_thaw',transaction=tx,account='b',**fence(loser))
    assert yes('one','hyper_status')['transaction']['stage']=='complete'
    # Abandon uses the same durable barrier, destroys one H and charges no E.
    tx=yes('one','hyper_begin',account='a',action='abandon',forgeKey='rose')['transaction']['id']
    yes('one','hyper_ready',transaction=tx,account='a',**fence(after))
    yes('one','hyper_progress',transaction=tx)
    abandoned=yes('one','hyper_status')['accounts']['a']
    assert abandoned['record']['inventory'].get('apex',{}).get('petal_rose',0)==0
    assert abandoned['record']['inventory']['unique']['petal_rose']==15
    yes('one','hyper_thaw',transaction=tx,account='a',**fence(abandoned))
    # Requester death is recovered only by a supervisor; then pending exchange
    # aborts with no cost, and the remaining game can safely thaw.
    tx=yes('one','hyper_begin',account='a',action='forge',input='rose')['transaction']['id']
    serial+=1
    process.stdin.write(json.dumps({'id':str(serial),'node':'supervisor-one','boot':'recovery','supervisor':True,'request':{
        'op':'recover_stopped','requestId':'recover','account':'a','stoppedShard':'one','stoppedBoot':'boot-one','epoch':abandoned['epoch']}})+'\n');process.stdin.flush()
    assert json.loads(process.stdout.readline())['result']['ok']
    aborted=yes('two','hyper_progress',transaction=tx)['transaction']
    assert aborted['stage']=='complete' and not aborted['result']['ok']
    snapshot=yes('one','snapshot')['state'];assert snapshot['accounts']['a']['record']==abandoned['record']
    # Operator-only temporary compensation is matched to the persisted request,
    # can target an offline account, and cannot be issued twice.
    yes('one','global_patch',globalRevision=snapshot['globalRevision'],writes=[],patch={'globals':{'operatorHyperForgeRequest':{'id':'operator-once','userId':'a','mode':'temporary15h','status':'pending'}}})
    op=yes('one','hyper_operator_begin',operatorRequest='operator-once')['transaction']['id']
    done=yes('two','hyper_progress',transaction=op)['transaction'];assert done['result']['ok'] and done['stage']=='complete'
    compensated=yes('one','snapshot')['state'];assert compensated['accounts']['a']['record']['inventory']['apex']['petal_mjolnir']==1
    assert compensated['globals']['operatorHyperForgeRequest']['status']=='done'
    assert not call('two','hyper_operator_begin',operatorRequest='operator-once')[0]['ok']
    process.stdin.close();assert process.wait(timeout=10)==0
print('PASS: cross-shard freeze/save/settle/thaw, stale writes, unique H/refund, restart replay, abandon, stopped-owner abort')
