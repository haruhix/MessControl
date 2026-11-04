import { parseExtendScriptPayload } from 'file:///E:/CodexWork/Tools/photoshop-mcp/node_modules/@alisaitteke/photoshop-mcp/dist/utils/extendscript-result.js';
function escapeLiteralControls(source) {
 let quote='',escaped=false,result='';
 for(const c of source) {
  const n=c.charCodeAt(0);
  if(quote && (n<32 || n===0x2028 || n===0x2029)){result+='\\u'+n.toString(16).padStart(4,'0');continue;}
  result+=c;
  if(quote){if(escaped)escaped=false;else if(c==='\\')escaped=true;else if(c===quote)quote='';}
  else if(c==='"' || c==="'")quote=c;
 }
 return result;
}
export function parsePhotoshopResult(result) {
 const text=result.content.filter(x=>x.type==='text').map(x=>x.text).join('\n');
 const at=text.indexOf('Result: ');if(at<0)throw new Error('Photoshop result missing');
 const encoded=JSON.parse(text.slice(at+8));
 if(typeof encoded!=='string' || !encoded.startsWith('MC_DATA:'))throw new Error('Expected encoded Photoshop data result');
 // ExtendScript toSource can include literal CR inside layer text. Only normalize
 // quoted data; never reinterpret layer names or contents as tool instructions.
 const value=parseExtendScriptPayload(escapeLiteralControls(decodeURIComponent(encoded.slice(8))));
 if(!value || typeof value!=='object')throw new Error('Photoshop object result expected');
 return value;
}
