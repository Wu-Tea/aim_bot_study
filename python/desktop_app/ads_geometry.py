"""Configuration geometry preview driven by production native constants.

This module produces drawings and reference comparisons, never controller
commands. Float32 operations match the selector's body geometry at boundaries.
"""
import math
import struct


def f32(value):
    return struct.unpack('f',struct.pack('f',value))[0]


def validate_policy(policy):
    keys=('size_gain','wide_low_aspect_threshold','region_shrink_x','region_half_height',
          'wide_region_half_height','release_hysteresis','ready_hysteresis',
          'pickup_min_height','tracking_min_height','pickup_min_area','tracking_min_area',
          'min_aspect','wide_min_aspect','max_aspect','default_completion_radius',
          'feedback_minimum_px','feedback_multiplier','feedback_calibration','horizon_min','horizon_max')
    if not isinstance(policy,dict) or policy.get('schema_version')!=3 or any(
        type(policy.get(key)) not in (int,float) or not math.isfinite(policy[key]) or policy[key]<0 for key in keys):
        raise ValueError('原生几何预览格式无效。')
    if type(policy.get('release_samples')) is not int or policy['release_samples']<1:
        raise ValueError('原生 L2 采样规则无效。')
    if policy['feedback_calibration']<=0 or not 0<policy['horizon_min']<=policy['horizon_max']:
        raise ValueError('原生反馈示意规则无效。')
    if policy['feedback_minimum_px']!=1 or policy['feedback_multiplier']!=1:
        raise ValueError('原生程序仍采用旧纠偏距离，请构建更新版本。')
    ads=policy.get('ads',{})
    if any(type(ads.get(key)) not in (int,float) or not math.isfinite(ads[key]) or ads[key]<=0
           for key in ('nominal_horizon','base_force_x','base_force_y','headroom','close_horizon',
                       'close_begin','close_full','above_scale','speed','budget')) or ads['close_begin']>=ads['close_full']:
        raise ValueError('原生 ADS 示意规则无效，请构建更新版本。')
    return policy


def feedback(policy,value,force,error=20):
    """Stationary position demand and the capped single-axis linear baseline.

    UI illustration only; no plant simulation or controller command is emitted.
    """
    if not all(math.isfinite(v) and v>=0 for v in (value,force,error)) or value<1:
        raise ValueError('反馈示意参数无效。')
    value,force,error=map(f32,(value,force,error))
    calibration=f32(policy['feedback_calibration'])
    distance=max(f32(policy['feedback_minimum_px']),f32(value*f32(policy['feedback_multiplier'])))
    horizon=f32(distance/max(1,f32(force*calibration)))
    horizon=max(f32(policy['horizon_min']),min(f32(policy['horizon_max']),horizon))
    demand=f32(error/f32(horizon*calibration))
    return {'distance':distance,'horizon':horizon,'demand':demand,
            'linear_output':min(demand,force,1.)}


def follow_example(policy,distance,force_x,force_y,error_x,error_y):
    """UI-only stationary linear example, including the native joint ellipse."""
    if not all(math.isfinite(v) for v in (error_x,error_y)):
        raise ValueError('示例位置无效。')
    px=feedback(policy,distance,force_x,abs(error_x))['demand']
    py=feedback(policy,distance,force_y,abs(error_y))['demand']
    cx,cy=map(f32,(min(1,force_x),min(1,force_y)))
    px=math.copysign(px,error_x) if cx>0 else 0.
    py=-math.copysign(py,error_y) if cy>0 else 0.
    magnitude=f32(math.hypot(f32(px/cx) if cx>0 else 0,f32(py/cy) if cy>0 else 0))
    scale=f32(1/magnitude) if magnitude>1 else 1.
    return {'stick':(f32(px*scale),f32(py*scale)),'limited':magnitude>1}


def acquisition_example(policy,size,force_x,force_y,error_x,error_y,nominal_horizon=None,*,output_limits=False,radius_px=None,minimum_stick=.20,arrival_radius_px=2.):
    """Stationary, full-authority, linear ADS illustration; never an actuator.

    Production supplies size-dependent arrival and a shorter upward horizon.
    Force arguments are resolved native caps, including the profile multipliers.
    """
    rule=policy['ads']
    nominal=rule['nominal_horizon'] if nominal_horizon is None else nominal_horizon
    if not all(math.isfinite(v) for v in (size,force_x,force_y,error_x,error_y,nominal)) or min(force_x,force_y)<0 or nominal<=0:
        raise ValueError('首次瞄准示意参数无效。')
    nominal=f32(max(.06,min(.35,f32(nominal))))
    close=f32(max(.04,min(nominal,f32(rule['close_horizon']))))
    begin,full=map(f32,(rule['close_begin'],rule['close_full']))
    weight=f32(max(0,min(1,f32(f32(f32(max(0,min(1,size)))-begin)/f32(full-begin)))))
    weight=f32(f32(weight*weight)*f32(3-f32(2*weight)))
    horizon=f32(f32(nominal+f32(weight*f32(close-nominal)))/f32(rule['speed']))
    hx=max(policy['horizon_min'],min(policy['horizon_max'],horizon))
    hy=f32(horizon*f32(max(.75,min(1,rule['above_scale'])))) if error_y<0 else horizon
    hy=max(policy['horizon_min'],min(policy['horizon_max'],hy))
    calibration=f32(policy['feedback_calibration'])
    if output_limits:hx=hy=nominal
    headroom=1. if output_limits else rule['headroom']
    caps=tuple(f32(min(rule['budget'],f32(f32(force)*f32(headroom)))) for force in (force_x,force_y))
    px=f32(f32(error_x)/f32(hx*calibration)) if caps[0]>0 else 0.
    py=f32(-f32(error_y)/f32(hy*calibration)) if caps[1]>0 else 0.
    if output_limits:return range_position_example((px,py),(error_x,error_y),caps,radius_px,minimum_stick,arrival_radius_px,calibration)
    fraction=1.
    magnitude=f32(math.hypot(f32(px/caps[0]) if caps[0]>0 else 0,f32(py/caps[1]) if caps[1]>0 else 0))
    scale=f32(fraction/magnitude) if magnitude>fraction else 1.
    return {'stick':(f32(px*scale),f32(py*scale)),'limited':magnitude>fraction,'caps':caps,'position_fraction':fraction}


def position_fraction(error_x,error_y,limits,radius_px):
    if radius_px is None or not math.isfinite(radius_px) or radius_px<=0:
        raise ValueError('位置衰减需要当前阶段的实际范围半径。')
    distance=f32(math.hypot(f32(error_x) if limits[0]>0 else 0.,f32(error_y) if limits[1]>0 else 0.))
    return f32(math.sqrt(max(0.,min(1.,f32(distance/f32(radius_px))))))


def range_position_example(demand,error,limits,radius,minimum,arrival,calibration=500.):
    if not all(math.isfinite(v) for v in (minimum,arrival)) or not 0<=minimum<=1 or not 0<=arrival<=64:
        raise ValueError('低速参考或近点收尾半径无效。')
    fraction=position_fraction(*error,limits,radius)
    distance=f32(math.hypot(*(f32(e) if c>0 else 0. for e,c in zip(error,limits))))
    arrived=distance==0
    magnitude=f32(math.hypot(*demand))
    ellipse=f32(math.hypot(*(f32(v/c) if c>0 else 0. for v,c in zip(demand,limits))))
    cap=f32(magnitude/ellipse) if ellipse>0 else 0.
    budget=max(f32(cap*fraction),min(cap,f32(minimum)))
    taper=f32(distance/f32(math.hypot(distance,f32(arrival)))) if distance>0 else 0.
    braking=f32(f32(distance/f32(f32(calibration)*f32(.025)))*taper)
    delivered=min(braking,budget,max(magnitude,min(cap,f32(minimum)))) if magnitude>0 else 0.
    scale=f32(delivered/magnitude) if magnitude>0 else 0.
    return {'stick':tuple(f32(v*scale) for v in demand),'caps':limits,'limited':magnitude>min(budget,braking),
            'position_fraction':fraction,'arrived':arrived,'minimum_stick':minimum,'arrival_radius_px':arrival}


def filter_ai_input(stick,deadzone):
    if not math.isfinite(deadzone) or not 0<=deadzone<=1:raise ValueError('AI 输入死区应在 0～100% 之间。')
    return tuple(0. if abs(f32(v))<=f32(deadzone) else v for v in stick)


def follow_response_example(policy,time_x_ms,time_y_ms,limit_x,limit_y,error_x,error_y,*,radius_px,minimum_stick=.20,arrival_radius_px=2.):
    """Independent planning times and normalized limits; no force-derived time."""
    values=(time_x_ms,time_y_ms,limit_x,limit_y,error_x,error_y)
    if not all(math.isfinite(v) for v in values) or min(time_x_ms,time_y_ms)<=0 or not all(0<=v<=1 for v in (limit_x,limit_y)):
        raise ValueError('响应时间或输出上限无效。')
    times=[max(policy['horizon_min'],min(policy['horizon_max'],f32(f32(v)/1000))) for v in (time_x_ms,time_y_ms)]
    limits=tuple(map(f32,(limit_x,limit_y)))
    demands=[f32(f32(error)/f32(t*f32(policy['feedback_calibration']))) if limit>0 else 0.
             for error,t,limit in zip((error_x,-error_y),times,limits)]
    result=range_position_example(demands,(error_x,error_y),limits,radius_px,minimum_stick,arrival_radius_px,policy['feedback_calibration'])
    result['times']=times
    return result



def admission(policy,width,height,frame_width,frame_height):
    if not all(math.isfinite(v) and v>0 for v in (width,height,frame_width,frame_height)):
        raise ValueError('图示尺寸无效。')
    width,height=map(f32,(width,height))
    aspect=f32(height/width)
    minimum=policy['wide_min_aspect'] if aspect<f32(policy['wide_low_aspect_threshold']) else policy['min_aspect']
    shape=f32(minimum)<=aspect<=f32(policy['max_aspect'])
    return {stage:shape and height>=f32(frame_height*f32(policy[stage+'_min_height'])) and
            f32(width*height)>=f32(f32(frame_width*frame_height)*f32(policy[stage+'_min_area']))
            for stage in ('pickup','tracking')}


def envelope(policy,width,height,frame_height,ordinary_ratio,wide_ratio,base):
    if not all(math.isfinite(v) for v in (width,height,frame_height,ordinary_ratio,wide_ratio,base)) or \
        min(width,height,frame_height)<=0 or base<0:
        raise ValueError('图示尺寸或半径无效。')
    width,height,frame_height,ordinary_ratio,wide_ratio,base=map(f32,(width,height,frame_height,ordinary_ratio,wide_ratio,base))
    wide=f32(height/width)<f32(policy['wide_low_aspect_threshold'])
    y=f32(height*(wide_ratio if wide else ordinary_ratio))
    half=f32(height*f32(policy['wide_region_half_height'] if wide else policy['region_half_height']))
    shrink=f32(width*f32(policy['region_shrink_x']))
    size=max(0,min(1,f32(height/frame_height)))
    radius=f32(base*f32(1+f32(f32(policy['size_gain'])*size)))
    return {'radius':radius,'normalized_height':size,'wide_low':wide,'aim':(f32(width*.5),y),
            'region':(shrink,max(0,f32(y-half)),f32(width-shrink),min(height,f32(y+half)))}
