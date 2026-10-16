"""Source-locked canonical-open gingiva; CPU preparation and isolated Blender build.

Prepare with ordinary Python --prepare. Build only under the lead's serialized
Blender slot. Writes Gums/ only; never edits jaws or the frozen oral interior.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import sys

# CPU source preparation is allowed while another serialized DCC job runs.
# Keep numerical libraries bounded as well as the eventual Blender renderer.
for _name in ('OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','OMP_NUM_THREADS'):
    os.environ[_name]='2'

ROOT=Path(r'E:\DEVGAME\MessControl\ArtSource\UI\MainMenu3D\20261015_refB_v1')
BASELINE=ROOT/'Gums/V02'
OUT=ROOT/'Gums/V03'
SOURCE=ROOT/'Textures/Revisions/v05_SourceLocked/Gingiva/gingiva_source_inventory.json'
TEETH=ROOT/'Evidence/SourceAnalysis/CanonicalOpen/TeethRefined/Vector_v09/part_inventory.json'
CONSTRAINTS=OUT/'Constraints/source_locked_gums.json'
WIDTH=16.;HEIGHT=410/963*16
# Both surfaces share their complete perimeter. Near-rim physical samples
# converge on the same vertical tangent rather than bridge a flat neck cap.
SURFACE_T=[0,.001,.004,.012,.030,.065,.12,.20,.30,.42,.55,.68,.80,.90,.96,.99,1.]


def arguments():
    args=sys.argv[sys.argv.index('--')+1:]if'--'in sys.argv else sys.argv[1:]
    parser=argparse.ArgumentParser()
    parser.add_argument('--prepare',action='store_true')
    parser.add_argument('--preview',action='store_true')
    return parser.parse_args(args)


def polygon_centroid(points):
    area=cx=cy=0.
    for a,b in zip(points,points[1:]+points[:1]):
        q=a[0]*b[1]-b[0]*a[1];area+=q;cx+=(a[0]+b[0])*q;cy+=(a[1]+b[1])*q
    return[cx/(3*area),cy/(3*area)]if abs(area)>1e-9 else[sum(p[i]for p in points)/len(points)for i in(0,1)]


def pchip(points,x):
    import bisect
    if x<=points[0][0]:return points[0][1]
    if x>=points[-1][0]:return points[-1][1]
    xx=[p[0]for p in points];yy=[p[1]for p in points]
    h=[b-a for a,b in zip(xx,xx[1:])];d=[(b-a)/w for a,b,w in zip(yy,yy[1:],h)]
    slopes=[d[0]]
    for i in range(1,len(points)-1):
        if d[i-1]*d[i]<=0:slopes.append(0.)
        else:
            w1=2*h[i]+h[i-1];w2=h[i]+2*h[i-1]
            slopes.append((w1+w2)/(w1/d[i-1]+w2/d[i]))
    slopes.append(d[-1]);i=bisect.bisect_right(xx,x)-1;t=(x-xx[i])/h[i]
    return(2*t**3-3*t*t+1)*yy[i]+(t**3-2*t*t+t)*h[i]*slopes[i]+(-2*t**3+3*t*t)*yy[i+1]+(t**3-t*t)*h[i]*slopes[i+1]


def edge_y(part,px,py):
    cx=part['centroid_xy'][0];bx,by,bw,bh=part['bbox_xywh']
    arch=(1.9 if part['name'].startswith('Lower') else 1.1)*(abs((cx/963-.5)*16)/8)**2.3
    fraction=(py-by)/max(bh,1)
    return arch+.18-(.45*fraction*fraction if'Fang'in part['name']else .045*math.sin(math.pi*fraction))


def smootherstep(t):
    t=max(0.,min(1.,t))
    return t*t*t*(10+t*(-15+6*t))


def physical_pixel_y(py,lower):
    """C2 hidden continuation; all measured cervical py stays unchanged."""
    if not lower:
        if py<=0:return 0.
        if py<10:return py*smootherstep(py/10)
        return py
    if py<=410:return py
    if py>=450:return 430.
    t=(py-410)/40
    return 410+40*(t-t**3+.5*t**4)


def continuous_edge_y(px,py,lower):
    """C2 ownership blending in X, evaluated at physical Z, never margin Z.

    Every observed upper crown arc uses the jaw's original edgeY equation.
    Only the unobserved interdental intervals blend between those equations.
    This removes V01's quantized margin-height transport and hard crown switch.
    """
    parts=sorted((p for p in FIT['crown_depth_contract']if p['name'].startswith('Lower')==lower),key=lambda p:p['centroid_xy'][0])
    py=physical_pixel_y(py,lower)
    offset=.025 if lower else -.015
    if lower:
        value=edge_y(parts[0],px,py)
        for left,right in zip(parts,parts[1:]):
            midpoint=(left['centroid_xy'][0]+right['centroid_xy'][0])/2
            halfwidth=min(12.,(right['centroid_xy'][0]-left['centroid_xy'][0])*.20)
            if px<midpoint-halfwidth:return value+offset
            if px<=midpoint+halfwidth:
                t=smootherstep((px-midpoint+halfwidth)/(2*halfwidth))
                return lerp(edge_y(left,px,py),edge_y(right,px,py),t)+offset
            value=edge_y(right,px,py)
        return value+offset
    arcs=FIT['upper_neck_ownership_intervals']
    by_name={p['name']:p for p in parts}
    x=(px/963-.5)*16
    posterior=1.1*(abs(x)/8)**2.3+.18
    first,last=arcs[0],arcs[-1]
    if px<first['x0']:
        w=smootherstep((px-(first['x0']-25))/25)
        return lerp(posterior,edge_y(by_name[first['crown']],px,py),w)+offset
    if px>last['x1']:
        w=smootherstep((px-last['x1'])/25)
        return lerp(edge_y(by_name[last['crown']],px,py),posterior,w)+offset
    for i,arc in enumerate(arcs):
        if arc['x0']<=px<=arc['x1']:return edge_y(by_name[arc['crown']],px,py)+offset
        if i+1<len(arcs)and arc['x1']<px<arcs[i+1]['x0']:
            other=arcs[i+1];w=smootherstep((px-arc['x1'])/(other['x0']-arc['x1']))
            return lerp(edge_y(by_name[arc['crown']],px,py),edge_y(by_name[other['crown']],px,py),w)+offset
    raise RuntimeError('No cervical depth interval')


def rim_y(px,lower):
    """Lead-approved inferred-Y relaxation; source X/Z remains unchanged."""
    table=FIT.get('rim_y_smoothing')
    if not table:return continuous_edge_y(px,margin_pixel(px,lower),lower)
    values=table['lower'if lower else'upper'];position=(px-table['x0'])/table['step_source_px']
    position=max(1.,min(len(values)-3.001,position));i=int(position);t=position-i
    weights=((1-t)**3/6,(3*t**3-6*t*t+4)/6,(-3*t**3+3*t*t+3*t+1)/6,t**3/6)
    return sum(w*values[i+j-1]for j,w in enumerate(weights))


def nearest_vector(points,poly):
    """Exact subpixel distance/ownership, bounded temporary array size."""
    import numpy as np
    points=np.asarray(points,float);poly=np.asarray(poly,float)
    b=np.roll(poly,-1,axis=0);ab=b-poly;den=np.maximum(np.sum(ab*ab,axis=1),1e-12)
    dd=[];ss=[];tt=[]
    for offset in range(0,len(points),128):
        pp=points[offset:offset+128]
        t=np.clip(np.sum((pp[:,None,:]-poly[None,:,:])*ab[None,:,:],axis=2)/den,0,1)
        q=poly[None,:,:]+t[:,:,None]*ab[None,:,:]
        ds=np.sum((pp[:,None,:]-q)**2,axis=2);seg=np.argmin(ds,axis=1);row=np.arange(len(pp))
        dd.extend(np.sqrt(ds[row,seg]));ss.extend(seg);tt.extend(t[row,seg])
    return np.asarray(dd),np.asarray(ss),np.asarray(tt)


def sample_c2(field,points,origin):
    """Tensor cubic B-spline interpolation, C2 in physical source X/Z."""
    import numpy as np
    pos=np.asarray(points,float)-origin
    xx=np.clip(pos[:,0],1,field.shape[1]-3.001);yy=np.clip(pos[:,1],1,field.shape[0]-3.001)
    ix=xx.astype(int);iy=yy.astype(int);tx=xx-ix;ty=yy-iy
    def weights(t):return[(1-t)**3/6,(3*t**3-6*t*t+4)/6,(-3*t**3+3*t*t+3*t+1)/6,t**3/6]
    wx=weights(tx);wy=weights(ty);out=np.zeros(len(pos))
    for j in range(4):
        for i in range(4):out+=wx[i]*wy[j]*field[iy+j-1,ix+i-1]
    return out


def outer_pixel(px,lower):
    sign=-1 if lower else 1;x=(px/963-.5)*16
    z=sign*(5.85 if lower else 5.95)+sign*.08*(x/9.6)**2
    return 205-z*963/16


def prepare_implicit_fields():
    import cv2
    import numpy as np
    from PIL import Image,ImageDraw
    arrays={};reports=[]
    for lower in(False,True):
        key='lower'if lower else'upper';xs=FIT['x_columns_source']
        poly=np.asarray([(x,margin_pixel(x,lower))for x in xs]+[(x,outer_pixel(x,lower))for x in reversed(xs)],float)
        area=float(np.sum(poly[:,0]*np.roll(poly[:,1],-1)-poly[:,1]*np.roll(poly[:,0],-1)))
        baseline=[]
        for index,(px,py)in enumerate(poly):
            if index<len(xs):baseline.append(rim_y(px,lower))
            else:
                x=(px/963-.5)*16
                baseline.append((1.9 if lower else 1.1)*(abs(x)/8)**2.3+.68)
        baseline=np.asarray(baseline)
        pad=9;origin=np.floor(poly.min(axis=0)).astype(int)-pad;hi=np.ceil(poly.max(axis=0)).astype(int)+pad
        shape=(int(hi[1]-origin[1]+1),int(hi[0]-origin[0]+1));mask=np.zeros(shape,np.uint8)
        cv2.fillPoly(mask,[np.round(poly-origin).astype(np.int32)],1)
        inside=cv2.erode(mask,np.ones((3,3),np.uint8),borderType=cv2.BORDER_CONSTANT,borderValue=0)>0
        yy,xx=np.nonzero(inside);ids=np.full(shape,-1,np.int32);ids[yy,xx]=np.arange(len(xx))
        ne=np.stack([ids[yy-1,xx],ids[yy+1,xx],ids[yy,xx-1],ids[yy,xx+1]],axis=1);good=ne>=0;safe=np.maximum(ne,0)
        edgeyy,edgexx=np.nonzero((mask>0)&~inside)
        _,ss,tt=nearest_vector(np.column_stack((edgexx+origin[0],edgeyy+origin[1])),poly)
        edgevalues=baseline[ss]*(1-tt)+baseline[(ss+1)%len(poly)]*tt
        raster_y=np.zeros(shape);raster_y[edgeyy,edgexx]=edgevalues
        rhs=(raster_y[yy-1,xx]+raster_y[yy+1,xx]+raster_y[yy,xx-1]+raster_y[yy,xx+1])
        def A(v):return 4*v-(v[safe]*good).sum(axis=1)
        def solve(rhs,label,warmstart=None):
            x=np.zeros(len(xx))if warmstart is None else warmstart.copy()
            r=rhs-A(x);p=r.copy();rs=float(r@r);initial=max(rs,1e-30)
            for it in range(2500):
                ap=A(p);alpha=rs/max(float(p@ap),1e-30);x+=alpha*p;r-=alpha*ap;nr=float(r@r)
                if nr<initial*1e-13:break
                p=r+(nr/rs)*p;rs=nr
            residual=math.sqrt(nr/initial)
            if residual>1e-5:raise RuntimeError(f'{key} {label} Poisson residual {residual}')
            print(f'CPU_FIELD {key} {label}: {len(xx)} unknowns, {it+1} iterations, relres {residual:.3g}',flush=True)
            return x,it+1,residual
        fingerprint=hashlib.sha256(poly.tobytes()+baseline.tobytes()).hexdigest()
        cache=OUT/'Constraints'/f'{key}_poisson_raw.npz'
        cached=np.load(cache)if cache.is_file()else None
        if cached is not None and str(cached['fingerprint'])==fingerprint:
            u=cached['u'];by=cached['by'];it_u=int(cached['it_u']);it_y=int(cached['it_y']);res_u=float(cached['res_u']);res_y=float(cached['res_y'])
            print(f'CPU_FIELD {key}: exact source-domain raw solve cache reused',flush=True)
        else:
            #V03 is strictly Y-only. Reuse the exact frozen V02 domain solve
            #rather than touching the front footprint or bulk thickness field.
            refcache=BASELINE/'Constraints'/f'{key}_poisson_raw.npz'
            if refcache.is_file():
                old=np.load(refcache);u=old['u'];it_u=int(old['it_u']);res_u=float(old['res_u'])
                oldfields=np.load(BASELINE/'Constraints/implicit_fields.npz')
                assert np.array_equal(poly,oldfields[key+'_poly'])
                assert len(u)==len(xx)
                print(f'CPU_FIELD {key}: V02 positive-depth solve reused; X/Z fixed',flush=True)
            else:u,it_u,res_u=solve(np.ones(len(xx)),'positive depth')
            by,it_y,res_y=solve(rhs,'harmonic cervical baseline',old['by']if refcache.is_file()else None)
            np.savez_compressed(cache,u=u,by=by,it_u=it_u,it_y=it_y,res_u=res_u,res_y=res_y,fingerprint=fingerprint)
        field=np.zeros(shape,np.float32);field[yy,xx]=u;field=cv2.GaussianBlur(field,(0,0),1.15);field[mask==0]=0;field/=max(float(field.max()),1e-8)
        # An independent harmonic extension of the complete rim replaces the
        # rejected C(x)*Gaussian(vertical-distance) margin transport entirely.
        # High frequency cervical data decays through the physical X/Z domain.
        #Extend the boundary values outside the domain before filtering. An
        #unrelated global-arch fill would bleed a false depth ridge into the
        #neck, especially near narrow papillae and cropped hidden corners.
        seeds=np.ones(shape,np.uint8);seeds[edgeyy,edgexx]=0
        _,labels=cv2.distanceTransformWithLabels(seeds,cv2.DIST_L2,5,labelType=cv2.DIST_LABEL_PIXEL)
        lookup=np.zeros(int(labels.max())+1);lookup[labels[edgeyy,edgexx]]=edgevalues
        yfield=lookup[labels];yfield[yy,xx]=by
        yfield=cv2.GaussianBlur(yfield.astype(np.float32),(0,0),1.15)
        incoming=poly-np.roll(poly,1,axis=0);outgoing=np.roll(poly,-1,axis=0)-poly
        #Unit incident edges form the angle bisector even where a hidden side
        #edge is250px long and the adjoining source neck segment is0.3px.
        incoming/=np.maximum(np.linalg.norm(incoming,axis=1)[:,None],1e-10)
        outgoing/=np.maximum(np.linalg.norm(outgoing,axis=1)[:,None],1e-10)
        tangent=incoming+outgoing
        inward=np.column_stack((-tangent[:,1],tangent[:,0]))*(1 if area>0 else -1)
        inward/=np.maximum(np.linalg.norm(inward,axis=1)[:,None],1e-10)
        probes=[];distances=[];poly32=poly.astype(np.float32)
        for point,normal in zip(poly,inward):
            distance=4.
            while distance>.04 and cv2.pointPolygonTest(poly32,tuple(point+normal*distance),False)<=0:distance*=.5
            if cv2.pointPolygonTest(poly32,tuple(point+normal*distance),False)<=0:raise RuntimeError(f'No inward support at {point}')
            probes.append(point+normal*distance);distances.append(distance)
        pd,probe_seg,probe_t=nearest_vector(probes,poly)
        uslope=sample_c2(field,probes,origin)/np.maximum(pd,.02)
        probe_rim_y=baseline[probe_seg]*(1-probe_t)+baseline[(probe_seg+1)%len(poly)]*probe_t
        yslope=(sample_c2(yfield,probes,origin)-probe_rim_y)/np.maximum(pd,.02)
        #Subpixel, unobserved digital ownership cells cannot prescribe an
        #arbitrarily large tissue derivative. Bound the continuation only;
        #the exact source rimY and the physical harmonic bulk remain intact.
        yslope=np.clip(yslope,-.035,.035)
        for _ in range(5):
            uslope=(np.roll(uslope,1)+2*uslope+np.roll(uslope,-1))/4
            yslope=(np.roll(yslope,1)+2*yslope+np.roll(yslope,-1))/4
        arrays.update({key+'_u':field,key+'_baseline':yfield,key+'_poly':poly,key+'_rim_y':baseline,key+'_u_slope':uslope,key+'_y_slope':yslope,key+'_origin':origin.astype(float)})
        report={'body':key,'field_shape':list(shape),'samples_per_source_pixel':1,'unknowns':len(xx),'depth_iterations':it_u,'baseline_iterations':it_y,'depth_relative_residual':res_u,'baseline_relative_residual':res_y,'boundary_points':len(poly),'adaptive_inward_support_min_px':min(distances),'geometry_function':'shared exact rim; C2 harmonic physical X/Z baseline ± low-frequency amplitude * sqrt(positive physical Poisson field); no flat cap'}
        reports.append(report)
        # CPU numerical evidence only, not a Blender appearance acceptance.
        preview=np.uint8(np.clip(field,0,1)*255);colored=cv2.applyColorMap(preview,cv2.COLORMAP_VIRIDIS);colored[mask==0]=(22,15,24)
        im=Image.fromarray(cv2.cvtColor(colored,cv2.COLOR_BGR2RGB));draw=ImageDraw.Draw(im)
        draw.text((12,12),f'{key.upper()} / physical XZ depth field / CPU evidence',fill='white',stroke_width=1,stroke_fill='black')
        im.save(OUT/'Constraints'/f'{key}_implicit_depth.png')
    np.savez_compressed(OUT/'Constraints/implicit_fields.npz',**arrays)
    (OUT/'Constraints/implicit_field_report.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
    return reports


def prepare_reference_v02():
    global FIT,FIELDS
    import cv2
    import numpy as np
    from PIL import Image,ImageDraw
    cv2.setNumThreads(2)
    data=json.loads(SOURCE.read_text());inv=json.loads(TEETH.read_text())
    parts=[]
    for part in inv['parts']:
        parts.append({'name':part['name'],'centroid_xy':polygon_centroid(part['vector_xy']),'bbox_xywh':part['bbox_xywh'],'vector_xy':part['vector_xy']})
    by_name={p['name']:p for p in parts}
    # Broad observed cervical arcs are authoritative. Isolated raw-mask jumps
    # between envelopes are explicitly uncertain, as confirmed by their owner.
    supports={float(x):float(y)for side in('left','right')for x,y in data['posterior_source_neck_samples'][side]}
    attachment=[]
    for neck in data['upper_per_crown_margins']:
        p=by_name[neck['crown']]
        for x,y in neck['visible_neck_points_xy']:
            supports[float(x)]=float(y)
            attachment.append({'crown':p['name'],'pixel_xy':[x,y],'world_xyz':[(x/963-.5)*16,edge_y(p,x,y)-.015,(205-y)*16/963]})
    control=sorted(supports.items());upper=np.array([pchip(control,x)for x in range(963)],'float32')
    # Keep actual broad arcs while removing pixel quantization at their tops.
    upper_smooth=cv2.GaussianBlur(upper.reshape(1,-1),(7,1),.75).reshape(-1)
    # Source controls move by at most0.8px; no cross-tooth generic wave is used.
    upper_smooth=np.clip(upper_smooth,upper-.8,upper+.8)
    lower_control=data['lower_source_base_margin']['points_xy']
    lower=np.array([pchip(lower_control,x)for x in range(963)],'float32')
    # Extend cropped-side tissue as hidden volume rather than terminate a bar.
    xs=sorted(set([-96.3,1059.3]+[float(x)for x in range(-96,1060,3)]+list(supports)))
    table={'schema':'canonical_source_locked_gums.v2','status':'CPU source candidate; actual Blender front/volume gate required',
      'source_inventory':str(SOURCE),'source_inventory_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
      'tooth_inventory':str(TEETH),'tooth_inventory_sha256':hashlib.sha256(TEETH.read_bytes()).hexdigest(),
      'source_image':data['source'],'source_sha256':data['source_sha256'],'source_size':[963,410],
      'canonical_camera':{'width':16,'height':HEIGHT,'center':[0,0,0],'resolution':[963,410],'front':'negativeY looking positiveY; no tilt'},
      'upper_margin_y_by_x':upper_smooth.tolist(),'lower_margin_y_by_x':lower.tolist(),'x_columns_source':xs,
      'upper_neck_attachment_supports':attachment,'crown_depth_contract':parts,
      'upper_neck_ownership_intervals':[{'crown':neck['crown'],'x0':min(p[0]for p in neck['visible_neck_points_xy']),'x1':max(p[0]for p in neck['visible_neck_points_xy'])}for neck in data['upper_per_crown_margins']],
      'source_projection_uv':{'name':'UV_SourceProjection','U':'x/963','V':'1-y/410'},
      'attachment_policy':'Upper margin edgeY−.015; lower behind enamel at edgeY+.025. Exact rim formula matches jawv09. No separate toroidal neck ring.',
      'uncertainty_policy':'One-pixel dark interdental raw-mask ownership pockets do not become hard notches. Six broad cervical arcs remain independently measured; posterior/lower interfaces ±3px. No tall lower-gap fill above the basal guide.',
      'visible_margin_source':{'upper':'Six measured neck arcs + posterior supports; <=.8px local smoothing','lower':'Observed basal interface PCHIP, not conservative red enamel ownership mask'},
      'inferred_returns':{'upper_outer_z':5.95,'lower_outer_z':-5.85,'source_crop_edges_not_boundaries':True,'source_confidence_on_backs':0},
      'expected_triangles':'approximately80k–100k for both closed bodies, subject to actual metric audit',
      'geometry_policy':'One closed implicit volume, front/back share exact complete rim; C2 physical X/Z harmonic baseline and positive Poisson bulk. No separate flat cap, transported quantized margin, polar surface, generic sine edge, or glints as geometry.'}
    CONSTRAINTS.parent.mkdir(parents=True,exist_ok=True)
    CONSTRAINTS.write_text(json.dumps(table,indent=2),encoding='utf-8')
    source=Image.open(data['source']).convert('RGB');draw=ImageDraw.Draw(source)
    draw.line([(x,float(y))for x,y in enumerate(upper_smooth)],fill=(50,255,190),width=1)
    draw.line([(x,float(y))for x,y in enumerate(lower)],fill=(90,180,255),width=1)
    draw.text((325,114),'GINGIVA MARGINS / source constraints',fill='white',stroke_width=1,stroke_fill='black')
    source.save(OUT/'Constraints/source_margin_overlay.png')
    FIT=table
    prepare_implicit_fields();FIELDS=np.load(OUT/'Constraints/implicit_fields.npz')
    audits=[]
    for lower in(False,True):
        key='lower'if lower else'upper';mesh=surface_data(lower)
        np.savez_compressed(OUT/'Constraints'/f'{key}_surface.npz',vertices=mesh['vertices'],faces=mesh['faces'],uv=mesh['uv'],semantic=mesh['semantic'])
        audits.append(audit_surface(mesh,lower))
    (OUT/'Constraints/cpu_surface_audit.json').write_text(json.dumps(audits,indent=2),encoding='utf-8')
    print(json.dumps({'status':'source ready; no DCC launched; visual gate still required','constraints':str(CONSTRAINTS),'columns':len(xs),'upper_attachments':len(attachment),'surface_audits':audits},indent=2))


def prepare():
    """V03: relax inferred rim Y; freeze all V02 X/Z/topology/UV evidence."""
    global FIT,FIELDS
    import shutil
    import cv2
    import numpy as np
    cv2.setNumThreads(2)
    FIT=json.loads((BASELINE/'Constraints/source_locked_gums.json').read_text())
    proposal=json.loads((BASELINE/'Diagnostics/rim_y_smoothing_proposal.json').read_text())
    xx=proposal['bodies'][0]['samples_x_source'];step=xx[1]-xx[0]
    FIT['schema']='canonical_source_locked_gums.v3'
    FIT['status']='V03 CPU source candidate; Y-only sigma8 smoothing authorized; actual combined neck intersection review required'
    FIT['baseline_v02']=str(BASELINE)
    FIT['rim_y_smoothing']={'x0':xx[0],'step_source_px':step,'sigma_source_px':8.,'sigma_world':8*16/963,'evaluation':'C2 cubic B-spline from dense Gaussian samples','depth_is_inferred':True}
    for b in proposal['bodies']:FIT['rim_y_smoothing'][b['body']]=b['proposed_smooth_rim_Y']
    FIT['attachment_policy']='V02 source X/Z fixed. Inferred cervicalY Gaussian sigma8px, C2 interpolated; crown/tissue intersection must be reviewed jointly. Upper/lower max deviations are reported against assumed V02 rim, not measured source depth.'
    CONSTRAINTS.parent.mkdir(parents=True,exist_ok=True);CONSTRAINTS.write_text(json.dumps(FIT,indent=2),encoding='utf-8')
    shutil.copy2(BASELINE/'Constraints/source_margin_overlay.png',OUT/'Constraints/source_margin_overlay.png')
    prepare_implicit_fields();FIELDS=np.load(OUT/'Constraints/implicit_fields.npz')
    reports=[];invariants=[]
    for lower in(False,True):
        key='lower'if lower else'upper';mesh=surface_data(lower);old=np.load(BASELINE/'Constraints'/f'{key}_surface.npz')
        checks={'body':key,'vertex_XZ_byte_identical':np.array_equal(mesh['vertices'][:,[0,2]],old['vertices'][:,[0,2]]),'face_indices_byte_identical':np.array_equal(mesh['faces'],old['faces']),'UV0_byte_identical':np.array_equal(mesh['uv'],old['uv']),'face_semantics_identical':np.array_equal(mesh['semantic'],old['semantic']),'vertex_count_identical':len(mesh['vertices'])==len(old['vertices']),'max_mesh_Y_change_world':float(np.abs(mesh['vertices'][:,1]-old['vertices'][:,1]).max())}
        assert all(checks[name]for name in('vertex_XZ_byte_identical','face_indices_byte_identical','UV0_byte_identical','face_semantics_identical','vertex_count_identical')),checks
        invariants.append(checks);reports.append(audit_surface(mesh,lower))
        np.savez_compressed(OUT/'Constraints'/f'{key}_surface.npz',vertices=mesh['vertices'],faces=mesh['faces'],uv=mesh['uv'],semantic=mesh['semantic'])
    (OUT/'Constraints/cpu_surface_audit.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
    (OUT/'Constraints/V02_invariants.json').write_text(json.dumps(invariants,indent=2),encoding='utf-8')
    print(json.dumps({'status':'V03 CPU gate passed; no DCC launched','invariants':invariants,'surface_audits':reports},indent=2))


def lerp(a,b,t):return a*(1-t)+b*t


def margin_pixel(px,lower):
    curve=FIT['lower_margin_y_by_x'if lower else'upper_margin_y_by_x']
    if px<0:return curve[0]+(-px/96.3)*(-22 if lower else 12)
    if px>962:return curve[-1]+((px-962)/97.3)*(-20 if lower else 12)
    i=min(961,int(px));return lerp(curve[i],curve[i+1],px-i)


def implicit_values(points,lower):
    import numpy as np
    key='lower'if lower else'upper';poly=FIELDS[key+'_poly'];origin=FIELDS[key+'_origin']
    d,seg,t=nearest_vector(points,poly);other=(seg+1)%len(poly)
    slope=FIELDS[key+'_u_slope'][seg]*(1-t)+FIELDS[key+'_u_slope'][other]*t
    yslope=FIELDS[key+'_y_slope'][seg]*(1-t)+FIELDS[key+'_y_slope'][other]*t
    rim_y=FIELDS[key+'_rim_y'][seg]*(1-t)+FIELDS[key+'_rim_y'][other]*t
    w=np.clip((d-2)/4,0,1);w=w**3*(10+w*(-15+6*w))
    u=np.maximum(0,d*slope*(1-w)+sample_c2(FIELDS[key+'_u'],points,origin)*w)
    baseline=(rim_y+d*yslope)*(1-w)+sample_c2(FIELDS[key+'_baseline'],points,origin)*w
    return u,baseline


def front_depth(x,z,lower):
    # Larger fleshy alveolar volume replaces V01's flat .18-world emboss.
    # All depth modulation is aperiodic source-root-centred physical X/Z.
    sign=-1 if lower else 1;depth=.80 if lower else .96
    for p in FIT['crown_depth_contract']:
        if p['name'].startswith('Lower')!=lower:continue
        cx=(p['centroid_xy'][0]/963-.5)*16
        width=max(.40,min(.84,p['bbox_xywh'][2]*16/963*.60))
        zc=(205-margin_pixel(p['centroid_xy'][0],lower))*16/963+sign*.55
        depth+=(.25 if'Fang'in p['name']else .21)*math.exp(-((x-cx)/width)**2-((z-zc)/.85)**2)
    return depth


RECTS={
 False:{'front':(.025,.025,.975,.205),'back':(.025,.435,.475,.645),'neck':(.025,.675,.975,.720),'outer':(.025,.815,.475,.865),'left':(.025,.890,.240,.975),'right':(.260,.890,.475,.975)},
 True:{'front':(.025,.230,.975,.410),'back':(.505,.435,.955,.645),'neck':(.025,.745,.975,.790),'outer':(.505,.815,.955,.865),'left':(.505,.890,.720,.975),'right':(.740,.890,.955,.975)}}


def uv_rect(rect,u,v):return(lerp(rect[0],rect[2],u),lerp(rect[1],rect[3],v))


def split_rect(rect,back):
    #32.77px separation at2K allows16px bake dilation on either island.
    middle=(rect[1]+rect[3])/2;gap=.016
    return(rect[0],middle+gap/2,rect[2],rect[3])if back else(rect[0],rect[1],rect[2],middle-gap/2)


def surface_data(lower):
    import numpy as np
    xs=FIT['x_columns_source'];n=len(xs);rows=len(SURFACE_T)
    points=[]
    for t in SURFACE_T:
        points.extend((px,lerp(margin_pixel(px,lower),outer_pixel(px,lower),t))for px in xs)
    points=np.asarray(points,float);u,baseline=implicit_values(points,lower)
    xx=(points[:,0]/963-.5)*16;zz=(205-points[:,1])*16/963
    depth=np.asarray([front_depth(x,z,lower)for x,z in zip(xx,zz)])
    vertices=[];front=[];back=[]
    for j in range(rows):
        row=[]
        for i in range(n):
            k=j*n+i;boundary=j in(0,rows-1)or i in(0,n-1)
            if boundary:
                #The physical source rim itself has no duplicated vertices.
                if j==0:y=rim_y(points[k,0],lower)
                elif j==rows-1:y=(1.9 if lower else 1.1)*(abs(xx[k])/8)**2.3+.68
                else:
                    ya=rim_y(points[k,0],lower)
                    yb=(1.9 if lower else 1.1)*(abs(xx[k])/8)**2.3+.68
                    y=lerp(ya,yb,SURFACE_T[j])
            else:y=baseline[k]-depth[k]*math.sqrt(float(u[k]))
            row.append(len(vertices));vertices.append((xx[k],y,zz[k]))
        front.append(row)
    for j in range(rows):
        row=[]
        for i in range(n):
            k=j*n+i
            if j in(0,rows-1)or i in(0,n-1):row.append(front[j][i])
            else:
                y=baseline[k]+(1.24 if lower else 1.42)*math.sqrt(float(u[k]))
                row.append(len(vertices));vertices.append((xx[k],y,zz[k]))
        back.append(row)
    faces=[];uv=[];semantic=[]
    leftcut=next(i for i,x in enumerate(xs)if x>=-60)
    rightcut=next(i for i,x in enumerate(xs)if x>=1023)
    def face_uv(i,j,is_back):
        if j<2:
            tag='neck';rect=split_rect(RECTS[lower][tag],is_back)
            ua=(xs[i]-xs[0])/(xs[-1]-xs[0]);ub=(xs[i+1]-xs[0])/(xs[-1]-xs[0])
            va=SURFACE_T[j]/SURFACE_T[2];vb=SURFACE_T[j+1]/SURFACE_T[2]
        elif j>=rows-2:
            tag='outer';rect=split_rect(RECTS[lower][tag],is_back)
            ua=(xs[i]-xs[0])/(xs[-1]-xs[0]);ub=(xs[i+1]-xs[0])/(xs[-1]-xs[0])
            va=(SURFACE_T[j]-SURFACE_T[-2])/(1-SURFACE_T[-2]);vb=(SURFACE_T[j+1]-SURFACE_T[-2])/(1-SURFACE_T[-2])
        elif i<leftcut or i>=rightcut:
            tag='left'if i<leftcut else'right';rect=split_rect(RECTS[lower][tag],is_back)
            a,b=(xs[0],xs[leftcut])if tag=='left'else(xs[rightcut],xs[-1])
            ua=(xs[i]-a)/(b-a);ub=(xs[i+1]-a)/(b-a)
            va=(SURFACE_T[j]-SURFACE_T[2])/(SURFACE_T[-2]-SURFACE_T[2]);vb=(SURFACE_T[j+1]-SURFACE_T[2])/(SURFACE_T[-2]-SURFACE_T[2])
        else:
            tag='back'if is_back else'front';rect=RECTS[lower][tag]
            ua=(xs[i]-xs[leftcut])/(xs[rightcut]-xs[leftcut]);ub=(xs[i+1]-xs[leftcut])/(xs[rightcut]-xs[leftcut])
            va=(SURFACE_T[j]-SURFACE_T[2])/(SURFACE_T[-2]-SURFACE_T[2]);vb=(SURFACE_T[j+1]-SURFACE_T[2])/(SURFACE_T[-2]-SURFACE_T[2])
        mapping=[uv_rect(rect,ua,va),uv_rect(rect,ub,va),uv_rect(rect,ub,vb),uv_rect(rect,ua,vb)]
        return tag, mapping
    for is_back,grid in[(False,front),(True,back)]:
        for j in range(rows-1):
            for i in range(n-1):
                face=(grid[j][i],grid[j][i+1],grid[j+1][i+1],grid[j+1][i])
                tag,mapping=face_uv(i,j,is_back)
                if is_back:face=face[::-1];mapping=mapping[::-1]
                faces.append(face);uv.append(mapping);semantic.append(('back_'if is_back else'front_')+tag)
    return{'vertices':np.asarray(vertices,float),'faces':np.asarray(faces,'int32'),'uv':np.asarray(uv,float),'semantic':np.asarray(semantic),'front_count':(rows-1)*(n-1),'boundary_count':2*n+2*(rows-2)}


def audit_surface(mesh,lower):
    import collections
    import numpy as np
    vv=mesh['vertices'];ff=mesh['faces'];edges=collections.Counter()
    for face in ff:
        for a,b in zip(face,np.roll(face,-1)):edges[tuple(sorted((int(a),int(b))))]+=1
    n=mesh['front_count'];front=ff[:n]
    a=vv[front[:,0]][:,[0,2]];b=vv[front[:,1]][:,[0,2]];c=vv[front[:,2]][:,[0,2]];d=vv[front[:,3]][:,[0,2]]
    def cross(p,q):return p[:,0]*q[:,1]-p[:,1]*q[:,0]
    areas=np.r_[cross(b-a,c-a),cross(c-a,d-a)]/2
    # X/Z-grid orientation differs upper/lower, but may never fold or fan.
    sign=1 if float(np.median(areas))>0 else -1
    invalid=int(np.count_nonzero(areas*sign<=1e-12))
    uvs=mesh['uv'];outofbounds=int(np.count_nonzero((uvs<0)|(uvs>1)))
    upper_support=[]
    if not lower:
        by_name={p['name']:p for p in FIT['crown_depth_contract']}
        for support in FIT['upper_neck_attachment_supports']:
            px,py=support['pixel_xy'];actual_py=margin_pixel(px,False)
            actual_y=rim_y(px,False)
            upper_support.append((abs(actual_py-py),abs(actual_y-edge_y(by_name[support['crown']],px,actual_py)+.015)))
    triangles=np.r_[ff[:,[0,1,2]],ff[:,[0,2,3]]];p,q,r=(vv[triangles[:,i]]for i in range(3))
    volume=float(np.sum(np.einsum('ij,ij->i',p,np.cross(q,r)))/6)
    min_surface_area=float(np.linalg.norm(np.cross(q-p,r-p),axis=1).min()/2)
    key='lower'if lower else'upper';poly=FIELDS[key+'_poly']
    contour_area=abs(float(np.sum(poly[:,0]*np.roll(poly[:,1],-1)-poly[:,1]*np.roll(poly[:,0],-1))))/2*(16/963)**2
    report={'body':key,'vertices':len(vv),'quads':len(ff),'triangles':len(ff)*2,'nonmanifold_edges':sum(v!=2 for v in edges.values()),'loose_vertices':len(vv)-len(set(ff.ravel().tolist())),'projected_front_folded_or_degenerate_triangles':invalid,'projected_front_area_world2':float(np.abs(areas).sum()),'projected_contour_area_world2':contour_area,'projected_area_absolute_error':abs(float(np.abs(areas).sum())-contour_area),'minimum_front_triangle_area_world2':float(np.abs(areas).min()),'minimum_3D_triangle_area_world2':min_surface_area,'absolute_enclosed_volume_world3':abs(volume),'uv0_out_of_bounds_values':outofbounds,'source_neck_max_delta_px':max((p[0]for p in upper_support),default=None),'jaw_neck_equation_max_error_world':max((p[1]for p in upper_support),default=None),'bounds_min_xyz':vv.min(axis=0).tolist(),'bounds_max_xyz':vv.max(axis=0).tolist(),'single_shared_cervical_rim':True,'flat_cap_faces':0,'uv_semantic_face_counts':dict(collections.Counter(mesh['semantic'].tolist()))}
    assert report['nonmanifold_edges']==0 and report['loose_vertices']==0 and invalid==0 and outofbounds==0,report
    assert min_surface_area>1e-12 and abs(volume)>1 and report['projected_area_absolute_error']<1e-8,report
    return report


def build_body(lower):
    import numpy as np
    key='lower'if lower else'upper';cached=np.load(OUT/'Constraints'/f'{key}_surface.npz')
    vertices=cached['vertices'];faces=cached['faces'];uvs=cached['uv'];semantic=cached['semantic'].tolist()
    name='MC_DAY03_Lower_Gum'if lower else'MC_DAY03_Upper_Gum'
    mesh=bpy.data.meshes.new(name+'_Mesh');mesh.from_pydata(vertices.tolist(),[],faces.tolist());mesh.update()
    bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(mesh);bm.free()
    uv0=mesh.uv_layers.new(name='UV0');uv_values=np.empty((len(mesh.loops),2),float)
    for polygon,indices,mapping in zip(mesh.polygons,faces,uvs):
        lookup=dict(zip(indices.tolist(),mapping))
        for li in polygon.loop_indices:uv_values[li]=lookup[mesh.loops[li].vertex_index]
    uv0.data.foreach_set('uv',uv_values.ravel())
    source_uv=mesh.uv_layers.new(name='UV_SourceProjection');loop_vertices=np.empty(len(mesh.loops),'int32');mesh.loops.foreach_get('vertex_index',loop_vertices)
    source_values=np.column_stack((vertices[loop_vertices,0]/16+.5,.5+vertices[loop_vertices,2]/HEIGHT))
    source_uv.data.foreach_set('uv',source_values.ravel());mesh.uv_layers.active_index=0;uv0.active_render=True
    obj=bpy.data.objects.new(name,mesh);GEOMETRY.objects.link(obj);mesh.materials.append(GUM)
    root=bpy.data.objects.new('MC_Gingiva_Lower'if lower else'MC_Gingiva_Upper',None);GEOMETRY.objects.link(root);obj.parent=root
    mesh.polygons.foreach_set('use_smooth',[True]*len(mesh.polygons))
    obj['source_constraints']=str(CONSTRAINTS);obj['pose']='Canonical OPEN; preserve world transform when parented to runtime jaw'
    obj['uv_policy']='Unique UV0 across both MC_Gum objects; UV_SourceProjection secondary for front-only source transfer'
    obj['front_margin']='Source X/Z and UV0 locked; inferred Y sigma8 C2 smoothed; closed shared cervical rim, no flat cap or separate ring'
    return obj,{'name':name,'face_semantics':{tag:semantic.count(tag)for tag in set(semantic)},'uv_rectangles':RECTS[lower],'topology':'monotone X/Z strip; front/back share full perimeter; no radial fan or ellipse rings'}


def add_camera(name,location,target,width=16,ortho=True):
    from mathutils import Vector
    data=bpy.data.cameras.new(name);obj=bpy.data.objects.new(name,data);PREVIEW.objects.link(obj);obj.location=location
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    if ortho:data.type='ORTHO';data.ortho_scale=width
    else:data.lens=90
    return obj


def add_area(name,location,target,energy,size):
    from mathutils import Vector
    data=bpy.data.lights.new(name,'AREA');data.energy=energy;data.shape='DISK';data.size=size
    obj=bpy.data.objects.new(name,data);PREVIEW.objects.link(obj);obj.location=location;obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()


def main(preview):
    global bpy,bmesh,FIT,GEOMETRY,PREVIEW,GUM
    import bpy
    import bmesh
    if not CONSTRAINTS.is_file():raise RuntimeError('Run --prepare with ordinary Python first; no unconstrained generic gum build is permitted')
    FIT=json.loads(CONSTRAINTS.read_text());OUT.mkdir(exist_ok=True)
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene
    GEOMETRY=bpy.data.collections.new('MenuGingiva_Geometry');scene.collection.children.link(GEOMETRY)
    PREVIEW=bpy.data.collections.new('Gingiva_PreviewOnly');scene.collection.children.link(PREVIEW)
    GUM=bpy.data.materials.new('MC_Gum');GUM.use_nodes=True
    shader=GUM.node_tree.nodes.get('Principled BSDF');shader.inputs['Base Color'].default_value=(.44,.023,.035,1);shader.inputs['Roughness'].default_value=.31;shader.inputs['Coat Weight'].default_value=.15;shader.inputs['Subsurface Weight'].default_value=.035;shader.inputs['Subsurface Scale'].default_value=.04
    meshes=[];entries=[]
    for lower in(False,True):
        obj,entry=build_body(lower);meshes.append(obj)
        bm=bmesh.new();bm.from_mesh(obj.data);obj.data.calc_loop_triangles()
        entry.update({'vertices':len(obj.data.vertices),'triangles':len(obj.data.loop_triangles),'non_manifold_edges':sum(not e.is_manifold for e in bm.edges),'loose_vertices':sum(not v.link_edges for v in bm.verts),'uv_layers':[u.name for u in obj.data.uv_layers]});bm.free()
        assert entry['non_manifold_edges']==0 and entry['loose_vertices']==0,entry
        entries.append(entry)
    metrics={'revision':'Gums source-locked v03 Y-only smoothed shared rim','status':'Generated for actual clay/source-overlay review; not accepted material/assembly','blender':bpy.app.version_string,'triangles':sum(e['triangles']for e in entries),'parts':entries,'constraints':str(CONSTRAINTS),'canonical_camera':FIT['canonical_camera'],'attachment_policy':FIT['attachment_policy'],'uv0_padding':'Minimum .016 internal cap-island gap=32.77px at2K/65.54px at4K; dilation16px at2K fits. Separate front/back/neck/outer/end charts.','sourceUV_policy':'UV_SourceProjection may leave0–1 on inferred returns; clip source images, never repeat; all UV0 remains unique in0–1.','cpu_surface_audit':str(OUT/'Constraints/cpu_surface_audit.json'),'implicit_field_report':str(OUT/'Constraints/implicit_field_report.json')}
    source_bytes=Path(__file__).read_bytes();(OUT/'build_gums_source_locked.py').write_bytes(source_bytes)
    metrics['executed_source_sha256']=hashlib.sha256(source_bytes).hexdigest()
    metrics['surface_cache_sha256']={side:hashlib.sha256((OUT/'Constraints'/f'{side}_surface.npz').read_bytes()).hexdigest()for side in('upper','lower')}
    metrics['V02_invariants']=str(OUT/'Constraints/V02_invariants.json')
    metrics['inferred_rim_Y_change']='Gaussian sigma8sourcepx=.133world; X/Z/topology/UV unchanged. Joint crown/tissue intersection review still required; do not interpret previous Y support as source measurement.'
    (OUT/'GumsMetrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
    scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24;scene.cycles.use_denoising=True;scene.render.threads_mode='FIXED';scene.render.threads=2
    #This isolated invocation is only started after the lead's GPU slot grant.
    prefs=bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type='OPTIX';prefs.get_devices()
        for device in prefs.devices:device.use=device.type=='OPTIX'
        if any(device.use for device in prefs.devices):scene.cycles.device='GPU'
    except Exception as exc:print('OPTIX unavailable; bounded CPU2 fallback: '+str(exc),flush=True)
    scene.render.resolution_x=963;scene.render.resolution_y=410;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
    scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.025,.015,.02,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.18
    add_area('Clay_Key',(-6,-6,7),(0,0,2.8),1200,7);add_area('Clay_Fill',(6,-3,1),(0,0,0),500,5)
    scene.camera=add_camera('CanonicalOpen_Gingiva',(0,-24,0),(0,0,0))
    bpy.ops.object.select_all(action='DESELECT')
    for obj in GEOMETRY.objects:obj.select_set(True)
    bpy.context.view_layer.objects.active=meshes[0]
    bpy.ops.export_scene.gltf(filepath=str(OUT/'Gums_SourceLocked.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
    bpy.ops.wm.obj_export(filepath=str(OUT/'Gums_SourceLocked.obj'),export_selected_objects=True,forward_axis='Y',up_axis='Z',apply_modifiers=True,export_materials=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Gums_SourceLocked.blend'))
    if preview:
        def render(name):scene.render.filepath=str(OUT/name);bpy.ops.render.render(write_still=True)
        render('Gums_OpenFront.png')
        matte=bpy.data.materials.new('Review_Gingiva_Matte');matte.use_nodes=True
        nodes=matte.node_tree.nodes;nodes.clear();em=nodes.new('ShaderNodeEmission');em.inputs['Color'].default_value=(1,1,1,1);output=nodes.new('ShaderNodeOutputMaterial');matte.node_tree.links.new(em.outputs[0],output.inputs['Surface'])
        scene.view_settings.view_transform='Standard';scene.cycles.samples=1;scene.cycles.use_denoising=False
        scene.world.node_tree.nodes['Background'].inputs[0].default_value=(0,0,0,1)
        for obj in meshes:obj.data.materials[0]=matte
        meshes[1].hide_render=True;render('Gums_UpperFrontMask.png');meshes[1].hide_render=False
        meshes[0].hide_render=True;render('Gums_LowerFrontMask.png');meshes[0].hide_render=False
        for obj in meshes:obj.data.materials[0]=GUM
        scene.view_settings.view_transform='AgX';scene.cycles.samples=24;scene.cycles.use_denoising=True
        scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.025,.015,.02,1)
        shader.inputs['Base Color'].default_value=(.33,.33,.33,1);shader.inputs['Coat Weight'].default_value=0;shader.inputs['Subsurface Weight'].default_value=0;shader.inputs['Roughness'].default_value=.44
        render('Gums_ClayFront.png')
        scene.render.resolution_x=1200;scene.render.resolution_y=450;scene.camera=add_camera('Upper_Gingiva_ClayClose',(0,-24,3.05),(0,0,3.05),9.7)
        meshes[1].hide_render=True;render('Gums_UpperClayClose.png');meshes[1].hide_render=False
        scene.render.resolution_x=1000;scene.render.resolution_y=650;scene.camera=add_camera('Gingiva_ThreeQuarter',(19,-50,17),(0,1.2,0),22,False)
        render('Gums_ClayThreeQuarter.png')
    print('RESULT '+json.dumps({'status':'generated_actual_review_required','out':str(OUT),'triangles':metrics['triangles'],'roots':['MC_Gingiva_Upper','MC_Gingiva_Lower']}),flush=True)


if __name__=='__main__':
    options=arguments()
    if options.prepare:prepare()
    else:main(options.preview)
