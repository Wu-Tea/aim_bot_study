// Scalar interpolation for curve-library previews; data arrives through the bridge.
export function interpolate(points, value, inverse = false) {
  const a = inverse ? 1 : 0, b = 1 - a;
  if (value <= 0) return 0;
  if (value >= 1) return 1;
  const i = points.findIndex((p) => p[a] >= value);
  const lo = points[i - 1], hi = points[i];
  return lo[b] + (hi[b] - lo[b]) * (value - lo[a]) / (hi[a] - lo[a]);
}

export const curvePath = (points, bottom = 360, height = 310) => points.map(([x, y], i) =>
  `${i ? 'L' : 'M'}${50 + x * 560},${bottom - y * height}`).join(' ');

export const curveWindow = (curve, key) => [curve[key]?.start ?? 0, curve[key]?.end ?? 0];
export function evaluateCurve(curve, value) {
  const [start, end] = curveWindow(curve, 'deadzone');
  const [lower, upper] = curveWindow(curve, 'compensation');
  if (value <= start) return 0;
  return lower + (1-lower-upper)*interpolate(curve.points, (value-start)/(1-start-end));
}
export function displayPoints(curve) {
  const [start, end] = curveWindow(curve, 'deadzone');
  const [lower, upper] = curveWindow(curve, 'compensation');
  const points = [[0, 0]];
  if (start) points.push([start, 0]);
  if (lower) points.push([start, lower]);
  points.push(...curve.points.slice(1).map(([x,y]) => [start+(1-start-end)*x, lower+(1-lower-upper)*y]));
  if (end) points.push([1, 1-upper]);
  return points;
}
export const runtimeMapping = (curve) => ({
  input_start: curve.deadzone?.start ?? 0, input_end: curve.deadzone?.end ?? 0,
  output_start: curve.compensation?.start ?? 0, output_end: curve.compensation?.end ?? 0,
});

export function curveLabel(curve) {
  const [start,end] = curveWindow(curve,'deadzone');
  return start || end ? `${curve.name}（死区 ${Number((start*100).toFixed(2))}% / ${Number((end*100).toFixed(2))}%）` : curve.name;
}
