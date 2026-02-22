#
#   Hello World server in Python
#   Binds REP socket to tcp://*:5555
#   Expects b"Hello" from client, replies with b"World"
#
import io
import time
import zmq
import numpy as np
import cv2
import json
import argparse
import tracemalloc
import gc
import linecache

from PIL import Image


def start_stream(address, topic="", display=False):
    # context = zmq.Context()
    context = zmq.Context.instance()
    socket = context.socket(zmq.SUB)
    # keep only last message
    socket.setsockopt(zmq.CONFLATE, 1)
    # socket.connect("tcp://localhost:50550")
    socket.connect(address)
    socket.setsockopt(zmq.SUBSCRIBE, topic.encode())
    socket.setsockopt(zmq.RCVTIMEO, 5000)
    socket.setsockopt(zmq.RCVHWM, 300)
    
    poller = zmq.Poller()
    poller.register(socket, zmq.POLLIN)
    fps_log = []
    last_print = time.time()
    try:
        while True:
            #  Wait for next request from client
            # poll 1000ms
            time_t0 = time.time()
            socks = dict(poller.poll(1000))
            if socks:
                if socks.get(socket) == zmq.POLLIN:
                    data = socket.recv()
                    img_np = np.array(Image.open(io.BytesIO(data)))
                    img_cv = cv2.cvtColor(img_np, cv2.COLOR_RGB2BGR)
                    diff = time.time() - time_t0
                    fps_log.append(1/diff)
                    fps_log = fps_log[-min(len(fps_log)-1, 10):]
                    if time.time() - last_print > 1:
                        print("Waiting time: {:.2f}ms, FPS: {:.2f}, {}".format(
                            1000*diff, np.average(fps_log), img_cv.shape
                        ))
                        last_print = time.time()
                    
                    if display:
                        cv2.imshow("stream", img_cv)
                        k = cv2.waitKey(1)
                        if k == ord("q"):
                            break

            else:
                print("error: message timeout")
                
    except (Exception, KeyboardInterrupt) as ex:
        print(f"Terminate Exception: {ex}")
        import traceback
        print(traceback.format_exc())
        # socket.setsockopt(zmq.LINGER, 0)
        # context.term()
        socket.close(linger=0)
        context.term()
    print("End")


def load_config(config_file):
    try:
        with open(config_file) as f:
            cfg = json.load(f)
        return cfg["streams"]["stream_1"]["zmq"]["address"]
    except Exception as ex:
        print(ex)
        raise ex

def display_top(snapshot1, snapshot2, key_type='lineno', limit=10):
    snapshot1 = snapshot1.filter_traces((
        tracemalloc.Filter(False, "<frozen importlib._bootstrap>"),
        tracemalloc.Filter(False, "<unknown>"),
    ))
    snapshot2 = snapshot2.filter_traces((
        tracemalloc.Filter(False, "<frozen importlib._bootstrap>"),
        tracemalloc.Filter(False, "<unknown>"),
    ))

    top_stats = snapshot2.compare_to(snapshot1, key_type)

    print(f"Top {limit} lines")
    for index, stat in enumerate(top_stats[:limit], 1):
        frame = stat.traceback[0]
        print(f"#{index}: {frame.filename}:{frame.lineno}: {stat.size_diff / 1024:.1f} KiB")
        line = linecache.getline(frame.filename, frame.lineno).strip()
        if line:
            print(f"  {line}")

    total = sum(stat.size_diff for stat in top_stats)
    print(f"Total allocated size: {total / 1024:.1f} KiB")

def start_with_trace(*args, **kwargs):
    # Start tracing with more frames for detailed tracebacks
    tracemalloc.start(25)
    snapshot1 = tracemalloc.take_snapshot()

    start_stream(*args, **kwargs)

    snapshot2 = tracemalloc.take_snapshot()
    # Force garbage collection to remove temporary objects and reduce noise
    gc.collect() 

    display_top(snapshot1, snapshot2)
    
    tracemalloc.stop()


def get_arguments():
    parser = argparse.ArgumentParser("Chrome headless capture")
    parser.add_argument("-c", "--config", type=str,
                        default="configs/g1bacc8_config.json",
                        help="Path to configuration file")
    
    parser.add_argument("-d", action="store_true",
                        default=False,
                        dest="display",
                        help="Display option")

    return vars(parser.parse_args())


if __name__ == '__main__':
    args = get_arguments()
    for k in args:
        print(" {}: {}".format(k, args[k]))

    zmq_add = load_config(config_file=args["config"])
    start_stream(zmq_add, display=args['display'])
    # start_with_trace(zmq_add, display=args['display'])
    # test_queue(config_file=args["config"])
