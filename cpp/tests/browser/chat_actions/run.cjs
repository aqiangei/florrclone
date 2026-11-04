const http=require('http'),fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
(async()=>{
 const root=process.env.CHAT_TEST_DIR||__dirname;
 const server=http.createServer((q,r)=>{const f=path.join(root,q.url==='/'?'index.html':q.url);r.setHeader('Content-Type',f.endsWith('.wasm')?'application/wasm':f.endsWith('.js')?'application/javascript':'text/html');fs.createReadStream(f).on('error',()=>r.end()).pipe(r);});
 await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
 try {const page=await browser.newPage({viewport:{width:1000,height:800}});page.on('pageerror',e=>{throw e});await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>window.uiReady);
 const hits=await page.evaluate(()=>window.chatHits);await page.screenshot({path:path.join(root,"before.png")});assert(hits.filter(x=>x.name.startsWith('LongPlayer')).length>=2,'wrapped author keeps target');assert.equal(hits.filter(x=>x.name==='Divitv').length,1,'body mention is not a target');
 const target=hits.find(x=>x.name==='hqyx');await page.mouse.click(target.x+target.w/2,target.y+target.h/2,{button:'right',delay:80});await page.waitForTimeout(100);await page.screenshot({path:path.join(root,'menu.png')});
 // Near the bottom, ContextMenu flips vertically above the pointer.
 const x=target.x+target.w/2,y=target.y+target.h/2-140;
 await page.mouse.click(x+40,y+5+26+13,{delay:80});await page.waitForTimeout(100);
 assert.equal(await page.evaluate(()=>window.chatDraft),'/w hqyx ');assert.equal(await page.evaluate(()=>window.chatCaret),8);console.log('PASS wrapped author / body isolation / whisper target and caret');
 }finally{await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exit(1)});
