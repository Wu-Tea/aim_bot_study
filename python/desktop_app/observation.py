"""Read runtime feedback off the Tk event thread; never own Tk objects."""
import queue
import threading
import time


class RuntimeObserver:
    def __init__(self,manager):
        self.manager=manager
        self.requests=queue.Queue(maxsize=1)
        self.results=queue.Queue()
        self.stopped=threading.Event()
        self.thread=threading.Thread(target=self.run,daemon=True,name='runtime-feedback')
        self.thread.start()

    def request(self,learning=False,fusion=False):
        if self.stopped.is_set():return
        try:self.requests.put_nowait((learning,fusion))
        except queue.Full:pass  # One pending observation is sufficient.

    def run(self):
        while not self.stopped.is_set():
            request=self.requests.get()
            if request is None or self.stopped.is_set():return
            learning,fusion=request
            sampled_at=time.monotonic()
            try:
                status=self.manager.status()
                result={'sampled_at':sampled_at,'status':status,'learning':self.manager.learning() if learning and status.get('record') else None,
                        'fusion':bool(self.manager.fusion_state()) if fusion else None}
            except Exception as error:
                result={'error':str(error)}
            if not self.stopped.is_set():self.results.put(result)

    def drain(self):
        latest=None
        while True:
            try:latest=self.results.get_nowait()
            except queue.Empty:return latest

    def close(self):
        self.stopped.set()
        try:self.requests.put_nowait(None)
        except queue.Full:pass
