"""Curve edit transactions, shared by pointer and precise numeric edits."""
from copy import deepcopy
from .curves import validate_points, MAX_POINTS


class CurveModel:
    def __init__(self, points, metadata=None):
        self.points = validate_points(points)
        self.metadata = deepcopy(metadata or {'algorithm':'custom_lut','name':'响应曲线'})
        self.selected = 1 if len(points) > 2 else 0
        self.undo_stack = []
        self.redo_stack = []
        self.origin = None

    def begin(self):
        self.origin = {'points':deepcopy(self.points),'metadata':deepcopy(self.metadata)}

    def commit(self):
        if self.origin is not None and self.origin != {'points':self.points,'metadata':self.metadata}:
            self.undo_stack.append(self.origin)
            self.undo_stack = self.undo_stack[-100:]
            self.redo_stack.clear()
        self.origin = None

    def replace(self, points, metadata=None):
        points = validate_points(points)
        self.begin()
        self.points = points
        if metadata is not None:self.metadata=deepcopy(metadata)
        self.selected = min(self.selected, len(points) - 1)
        self.commit()

    def exact(self, index, x, y):
        if not 0 < index < len(self.points) - 1:
            raise ValueError('端点固定为 0% 和 100%。')
        candidate = deepcopy(self.points)
        candidate[index] = [x, y]
        candidate = validate_points(candidate)
        self.begin()
        if candidate!=self.points:self.metadata['algorithm']='custom_lut'
        self.points = candidate
        self.selected = index
        self.commit()

    def drag(self, index, x, y):
        if not 0 < index < len(self.points) - 1:
            return
        previous, following = self.points[index - 1], self.points[index + 1]
        margin = .000011
        candidate = [min(following[0] - margin, max(previous[0] + margin, x)),
                     min(following[1] - margin, max(previous[1] + margin, y))]
        if candidate!=self.points[index]:self.metadata['algorithm']='custom_lut'
        self.points[index] = candidate
        self.selected = index

    def add(self, x, y):
        if len(self.points) == MAX_POINTS:
            raise ValueError('最多支持 32 个点。')
        index = next((i for i, p in enumerate(self.points) if p[0] > x), len(self.points))
        candidate = deepcopy(self.points)
        candidate.insert(index, [x, y])
        candidate = validate_points(candidate)
        self.begin()
        self.metadata['algorithm']='custom_lut'
        self.points = candidate
        self.selected = index
        self.commit()

    def remove(self):
        if not 0 < self.selected < len(self.points) - 1:
            raise ValueError('固定端点不能删除。')
        self.begin()
        self.metadata['algorithm']='custom_lut'
        self.points.pop(self.selected)
        self.selected = min(self.selected, len(self.points) - 2)
        self.commit()

    def undo(self):
        if self.undo_stack:
            self.redo_stack.append({'points':deepcopy(self.points),'metadata':deepcopy(self.metadata)})
            state = self.undo_stack.pop()
            self.points,self.metadata = state['points'],state['metadata']
            self.selected = min(self.selected, len(self.points) - 1)

    def redo(self):
        if self.redo_stack:
            self.undo_stack.append({'points':deepcopy(self.points),'metadata':deepcopy(self.metadata)})
            state = self.redo_stack.pop()
            self.points,self.metadata = state['points'],state['metadata']
            self.selected = min(self.selected, len(self.points) - 1)

    def cancel(self):
        if self.origin is not None:
            self.points,self.metadata=self.origin['points'],self.origin['metadata']
            self.origin=None
