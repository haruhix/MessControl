import { Client } from 'file:///E:/CodexWork/Tools/photoshop-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/index.js';
import { StdioClientTransport } from 'file:///E:/CodexWork/Tools/photoshop-mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/stdio.js';
import { readFile,writeFile,mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { parsePhotoshopResult } from './photoshop_payload.mjs';
const server='E:/CodexWork/Tools/photoshop-mcp',project='E:/DEVGAME/MessControl',evidence='E:/CodexWork/MessControl/Validation/UICatch_20261104';
const group=JSON.parse(await readFile(evidence+'/SourceLayers.json','utf8')).groups.find(x=>x.id===1265);
const output=project+'/ArtSource/UI/V2/Icons/Catch';
await mkdir(output,{recursive:true});
const plan=[['Fish',[1439],'Common'],['FishPair',[1582,1586],'Epic'],['Octopus',[1613],'Rare'],['Chest',[1639],'Legendary'],['WalletFish',[1440],''],['WalletCoin',[1258],'']].map(([name,ids,rarity])=>({name,rarity,file:output+'/T_UI_PSD_Catch_'+name+'.png',layers:ids.map(id=>{const row=group.layers.find(x=>x.id===id);if(!row)throw new Error('Missing layer '+id);return {id,name:row.name};})}));
const source='D:/sda/visua2.psd';
const beforeHash=createHash('sha256').update(await readFile(source)).digest('hex');
const client=new Client({name:'messcontrol-catch-glyph-export',version:'1'});
const transport=new StdioClientTransport({command:process.execPath,args:[server+'/node_modules/@alisaitteke/photoshop-mcp/dist/index.js'],cwd:server,env:{...process.env,TEMP:'E:/CodexTemp/photoshop-mcp',TMP:'E:/CodexTemp/photoshop-mcp',PHOTOSHOP_PATH:'D:/Software A/Adobe Photoshop 2025/Photoshop.exe',PHOTOSHOP_MCP_HOME:server+'/data',PHOTOSHOP_MCP_API:'extendscript',ANALYTICS_DISABLED:'1',PSMCP_UPDATE_CHECK:'0',PSMCP_FEEDBACK:'0'},stderr:'pipe'});
try {
 await client.connect(transport);await client.listTools();await client.callTool({name:'photoshop_ping',arguments:{}},undefined,{timeout:60000});
 const result=await client.callTool({name:'photoshop_execute_script',arguments:{timeout_ms:180000,code:`
 var previous=app.documents.length?app.activeDocument:null,doc=null;
 for(var d=0;d<app.documents.length;d++)if(app.documents[d].name==='visua2.psd')doc=app.documents[d];
 if(!doc)doc=app.open(new File('D:/sda/visua2.psd'));
 var savedBefore=doc.saved,work=null,tmp=null,exported=[],specs=${JSON.stringify(plan)},scale=4;
 function findNamed(parent,name) {
  for(var i=0;i<parent.layers.length;i++) {var l=parent.layers[i];if(l.name===name)return l;if(l.typename==='LayerSet'){var nested=findNamed(l,name);if(nested)return nested;}}
  return null;
 }
 try {
  var sourceIndex=-1;for(var i=0;i<doc.layerSets.length;i++)if(doc.layerSets[i].id===1265)sourceIndex=i;
  if(sourceIndex<0)throw new Error('Original UI_HUB artboard missing');
  work=doc.duplicate('MC_UI_Catch_Glyph_Export',false);app.activeDocument=work;
  var sourceGroup=work.layerSets[sourceIndex];
  for(var s=0;s<specs.length;s++){
   var spec=specs[s],layers=[],left=1e9,top=1e9,right=-1e9,bottom=-1e9;
   app.activeDocument=work;
   for(var k=0;k<spec.layers.length;k++){
    var layer=findNamed(sourceGroup,spec.layers[k].name);if(!layer)throw new Error('Glyph layer missing: '+spec.layers[k].name);
    var b=layer.bounds,rect=[b[0].as('px'),b[1].as('px'),b[2].as('px'),b[3].as('px')];
    left=Math.min(left,rect[0]);top=Math.min(top,rect[1]);right=Math.max(right,rect[2]);bottom=Math.max(bottom,rect[3]);layers.push({layer:layer,rect:rect});
   }
   var width=Math.ceil((right-left)*scale),height=Math.ceil((bottom-top)*scale);
   tmp=app.documents.add(width,height,72,'MC_Catch_'+spec.name,NewDocumentMode.RGB,DocumentFill.TRANSPARENT);
   var combined=tmp.layerSets.add();combined.name='Original glyph only';var copied=[];
   for(var k=0;k<layers.length;k++){
    app.activeDocument=work;var copy=layers[k].layer.duplicate(tmp,ElementPlacement.PLACEATBEGINNING);app.activeDocument=tmp;
    var cb=copy.bounds;copy.translate(layers[k].rect[0]-left-cb[0].as('px'),layers[k].rect[1]-top-cb[1].as('px'));copy.move(combined,ElementPlacement.INSIDE);copied.push(copy);
   }
   app.activeDocument=tmp;combined.resize(scale*100,scale*100,AnchorPosition.TOPLEFT);
   for(var k=0;k<copied.length;k++)if(copied[k].kind!==LayerKind.NORMAL)copied[k].rasterize(RasterizeType.ENTIRELAYER);
   var options=new PNGSaveOptions();options.compression=9;options.interlaced=false;
   tmp.saveAs(new File(spec.file),options,true,Extension.LOWERCASE);tmp.close(SaveOptions.DONOTSAVECHANGES);tmp=null;
   exported.push({name:spec.name,rarity:spec.rarity,sourceLayerIDs:spec.layers,sourceBounds:[left,top,right,bottom],exportScale:scale,size:[width,height],file:spec.file});
  }
 } finally {
  if(tmp)tmp.close(SaveOptions.DONOTSAVECHANGES);
  if(work)work.close(SaveOptions.DONOTSAVECHANGES);
  if(previous)app.activeDocument=previous;else app.activeDocument=doc;
 }
 return 'MC_DATA:'+encodeURIComponent(({source:doc.fullName.fsName,savedBefore:savedBefore,savedAfter:doc.saved,exported:exported}).toSource());
 `}},undefined,{timeout:180000});
 if(result.isError)throw new Error(JSON.stringify(result));
 await writeFile(evidence+'/GlyphExportRaw.json',JSON.stringify(result,null,2));
 const manifest=parsePhotoshopResult(result);
 const afterHash=createHash('sha256').update(await readFile(source)).digest('hex');
 if(beforeHash!==afterHash || manifest.savedBefore!==manifest.savedAfter)throw new Error('Source state changed unexpectedly');
 manifest.sourcePSD_SHA256=afterHash;manifest.method='Photoshop MCP; source document duplicated; only glyph Smart Objects copied, enlarged before rasterization, exported with transparency; no UI panel rasterization.';
 manifest.mappingNote='Tier artwork only. Existing runtime rarities retained; no species/item/category or reward-mechanic changes.';
 await writeFile(output+'/PSDIconManifest.json',JSON.stringify(manifest,null,2));
 console.log(JSON.stringify(manifest));
}finally{await client.close();}
