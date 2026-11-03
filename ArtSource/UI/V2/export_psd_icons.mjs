import { Client } from 'file:///E:/CodexWork/Tools/photoshop-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/index.js';
import { StdioClientTransport } from 'file:///E:/CodexWork/Tools/photoshop-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/stdio.js';
import { mkdir,readFile,writeFile } from 'node:fs/promises';
const root='E:/CodexWork/Tools/photoshop-mcp',project='E:/DEVGAME/MessControl';
const out='E:/CodexWork/MessControl/Validation/UIIconsPSD_20261103';
const metadata=JSON.parse(await readFile(out+'/SourceLayers.json','utf8'));
const group=metadata.groups.find(x=>x.name==='tools/busters');
const plan=[
 [713,'Spray','Tool.Spray'],[719,'Brush','Tool.Brush'],[726,'Pickaxe','Tool.Pickaxe'],[729,'Knife','Tool.Knife'],
 [748,'Watergun','ToolUpgrade.Watergun'],[742,'MeshaBrush','ToolUpgrade.MeshaBrush'],[749,'PickaxeVariant',''],[746,'Chainsaw','ToolUpgrade.Chainsaw'],
 [751,'Sponge','Active_Sponge'],[753,'Rocket','Consumable_Rocket'],[755,'Driver','Consumable_Driver'],[757,'Hook','Active_Hook'],[773,'Grenade','Consumable_Grenade']
].map(([id,name,key])=>{const layer=group.layers.find(x=>x.id===id);if(!layer)throw new Error('Missing source layer '+id);return {id,name,key,layerName:layer.name,file:project+'/ArtSource/UI/V2/Icons/T_UI_PSD_'+name+'.png'};});
await mkdir(project+'/ArtSource/UI/V2/Icons',{recursive:true});
const client=new Client({name:'messcontrol-original-psd-icon-export',version:'1'});
const transport=new StdioClientTransport({command:process.execPath,args:[root+'/node_modules/@alisaitteke/photoshop-mcp/dist/index.js'],cwd:root,env:{...process.env,TEMP:'E:/CodexTemp/photoshop-mcp',TMP:'E:/CodexTemp/photoshop-mcp',PHOTOSHOP_PATH:'D:/Software A/Adobe Photoshop 2025/Photoshop.exe',PHOTOSHOP_MCP_HOME:root+'/data',PHOTOSHOP_MCP_API:'extendscript',ANALYTICS_DISABLED:'1',PSMCP_UPDATE_CHECK:'0',PSMCP_FEEDBACK:'0'},stderr:'pipe'});
try {
 await client.connect(transport);await client.listTools();await client.callTool({name:'photoshop_ping',arguments:{}},undefined,{timeout:60000});
 const result=await client.callTool({name:'photoshop_execute_script',arguments:{timeout_ms:180000,code:`
 var previous=app.documents.length?app.activeDocument:null,doc=null;
 for(var d=0;d<app.documents.length;d++)if(app.documents[d].name==='visua2.psd')doc=app.documents[d];
 if(!doc)doc=app.open(new File('D:/sda/visua2.psd'));
 var savedBefore=doc.saved,tmp=null,work=null,exported=[],specs=${JSON.stringify(plan)};
 try {
  work=doc.duplicate('MC_UI_Original_Icon_Export',false);app.activeDocument=work;
  var group=work.layerSets.getByName('tools/busters');
  for(var i=0;i<specs.length;i++){
   var spec=specs[i];app.activeDocument=work;
   var source=group.artLayers.getByName(spec.layerName);source.rasterize(RasterizeType.ENTIRELAYER);
   var b=source.bounds,w=Math.ceil(b[2].as('px')-b[0].as('px')),h=Math.ceil(b[3].as('px')-b[1].as('px'));
   tmp=app.documents.add(w,h,72,'MC_UI_Icon_'+spec.name,NewDocumentMode.RGB,DocumentFill.TRANSPARENT);
   app.activeDocument=work;var copy=source.duplicate(tmp,ElementPlacement.PLACEATBEGINNING);app.activeDocument=tmp;
   var cb=copy.bounds;copy.translate(-cb[0].as('px'),-cb[1].as('px'));
   var options=new PNGSaveOptions();options.compression=9;options.interlaced=false;
   tmp.saveAs(new File(spec.file),options,true,Extension.LOWERCASE);tmp.close(SaveOptions.DONOTSAVECHANGES);tmp=null;
   exported.push({sourceLayerID:spec.id,sourceLayerName:spec.layerName,name:spec.name,key:spec.key,file:spec.file,size:[w,h]});
  }
 } finally {
  if(tmp)tmp.close(SaveOptions.DONOTSAVECHANGES);
  if(work)work.close(SaveOptions.DONOTSAVECHANGES);
  if(previous)app.activeDocument=previous;else app.activeDocument=doc;
 }
 return {source:doc.fullName.fsName,savedBefore:savedBefore,savedAfter:doc.saved,exported:exported};
 `}},undefined,{timeout:180000});
 if(result.isError)throw new Error(JSON.stringify(result));
 await writeFile(out+'/ExportRaw.json',JSON.stringify(result,null,2));
 const t=result.content.filter(x=>x.type==='text').map(x=>x.text).join('\n');
 const manifest=JSON.parse(t.slice(t.indexOf('Result: ')+8));
 await writeFile(project+'/ArtSource/UI/V2/Icons/PSDIconManifest.json',JSON.stringify(manifest,null,2));
 console.log(JSON.stringify(manifest));
}finally{await client.close();}
