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
