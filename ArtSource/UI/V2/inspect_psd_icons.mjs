import { Client } from 'file:///E:/CodexWork/Tools/photoshop-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/index.js';
import { StdioClientTransport } from 'file:///E:/CodexWork/Tools/photoshop-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/stdio.js';
import { mkdir,writeFile } from 'node:fs/promises';
const root='E:/CodexWork/Tools/photoshop-mcp';
const out='E:/CodexWork/MessControl/Validation/UIIconsPSD_20261103';
await mkdir(out,{recursive:true});
const client=new Client({name:'messcontrol-original-psd-icons',version:'1'});
const transport=new StdioClientTransport({command:process.execPath,args:[root+'/node_modules/@alisaitteke/photoshop-mcp/dist/index.js'],cwd:root,env:{...process.env,TEMP:'E:/CodexTemp/photoshop-mcp',TMP:'E:/CodexTemp/photoshop-mcp',PHOTOSHOP_PATH:'D:/Software A/Adobe Photoshop 2025/Photoshop.exe',PHOTOSHOP_MCP_HOME:root+'/data',PHOTOSHOP_MCP_API:'extendscript',ANALYTICS_DISABLED:'1',PSMCP_UPDATE_CHECK:'0',PSMCP_FEEDBACK:'0'},stderr:'pipe'});
try {
 await client.connect(transport);await client.listTools();
 const ping=await client.callTool({name:'photoshop_ping',arguments:{}},undefined,{timeout:60000});
 console.log(JSON.stringify({tool:'photoshop_ping',result:ping}));
 const result=await client.callTool({name:'photoshop_execute_script',arguments:{code:`
 var previous=app.documents.length?app.activeDocument:null, doc=null;
 for(var d=0;d<app.documents.length;d++) if(app.documents[d].name==='visua2.psd') doc=app.documents[d];
 if(!doc) doc=app.open(new File('D:/sda/visua2.psd'));
 var savedBefore=doc.saved;app.activeDocument=doc;var groups=[];
 function walk(parent,rows){for(var i=0;i<parent.layers.length;i++){
  var l=parent.layers[i],b=l.bounds,r={id:l.id,name:l.name,type:l.typename,visible:l.visible,bounds:[b[0].as('px'),b[1].as('px'),b[2].as('px'),b[3].as('px')]};
  if(l.typename==='ArtLayer'){r.kind=String(l.kind);if(l.kind===LayerKind.TEXT)r.text=l.textItem.contents;}
  rows.push(r);if(l.typename==='LayerSet')walk(l,rows);
 }}
 for(var g=0;g<doc.layerSets.length;g++) if(doc.layerSets[g].name==='tools/busters' || doc.layerSets[g].name==='GAMEPLAY UI'){
  var group=doc.layerSets[g],rows=[];walk(group,rows);groups.push({name:group.name,id:group.id,layers:rows});
 }
 var state={document:doc.name,savedBefore:savedBefore,savedAfter:doc.saved,groups:groups};
 if(previous)app.activeDocument=previous;return state;
 `}},undefined,{timeout:120000});
 if(result.isError)throw new Error(JSON.stringify(result));
 await writeFile(out+'/SourceLayersRaw.json',JSON.stringify(result,null,2));
 const text=result.content.filter(x=>x.type==='text').map(x=>x.text).join('\n');
 const parsed=JSON.parse(text.slice(text.indexOf('Result: ')+8));
 await writeFile(out+'/SourceLayers.json',JSON.stringify(parsed,null,2));
 console.log(JSON.stringify(parsed));
}finally{await client.close();}
