"""Export locked research candidates with the native runtime's float32 IO contract."""
import json
import shutil
import struct
import sys

from game_specialists import OUT, save_json, sha


def main(game):
    from ultralytics import YOLO
    import tensorrt as trt

    selected=json.loads((OUT/'evaluation/locked-selections.json').read_text())[game]
    dest=OUT/'exports'/game
    dest.mkdir(parents=True,exist_ok=False)
    pt=dest/f'{game}_specialist.pt'
    shutil.copy2(selected['path'],pt)
    model=YOLO(str(pt))
    onnx=model.export(format='onnx',imgsz=(384,480),batch=1,half=False,
                      dynamic=False,simplify=True,opset=17,device=0,nms=False)
    logger=trt.Logger(trt.Logger.WARNING)
    builder=trt.Builder(logger)
    network=builder.create_network(0)
    parser=trt.OnnxParser(network,logger)
    if not parser.parse_from_file(str(onnx)):
        raise RuntimeError('\n'.join(str(parser.get_error(i)) for i in range(parser.num_errors)))
    assert network.num_inputs==1 and network.num_outputs==1
    assert tuple(network.get_input(0).shape)==(1,3,384,480)
    assert tuple(network.get_output(0).shape)==(1,300,6)
    assert network.get_input(0).dtype==trt.float32 and network.get_output(0).dtype==trt.float32
    config=builder.create_builder_config()
    config.set_memory_pool_limit(trt.MemoryPoolType.WORKSPACE,2<<30)
    config.set_flag(trt.BuilderFlag.FP16)
    plan=builder.build_serialized_network(network,config)
    if plan is None: raise RuntimeError('TensorRT build failed')
    metadata=dict(description=f'{game} exploratory specialist; not deployed',names={'0':'person'},
                  task='detect',batch=1,imgsz=[384,480],half=True,end2end=True,
                  source_sha256=sha(pt),tensorrt=trt.__version__)
    encoded=json.dumps(metadata).encode()
    engine_path=dest/f'{game}_480x384.engine'
    engine_path.write_bytes(struct.pack('<I',len(encoded))+encoded+bytes(plan))
    runtime=trt.Runtime(logger)
    engine=runtime.deserialize_cuda_engine(plan)
    if engine is None: raise RuntimeError('Export could not be deserialized')
    bindings=[]
    for i in range(engine.num_io_tensors):
        name=engine.get_tensor_name(i)
        assert engine.get_tensor_dtype(name)==trt.float32
        bindings.append(dict(name=name,shape=list(engine.get_tensor_shape(name)),
                             dtype=str(engine.get_tensor_dtype(name))))
    save_json(dest/'export-manifest.json',dict(metadata,pt=str(pt),engine=str(engine_path),
              engine_sha256=sha(engine_path),onnx_sha256=sha(onnx),bindings=bindings))
    print(engine_path,flush=True)


if __name__=='__main__': main(sys.argv[1])
