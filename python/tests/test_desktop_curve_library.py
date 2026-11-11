import json
import random
import tempfile
import unittest
from pathlib import Path
from desktop_app.curves import CurveLibrary, compose_curves, interpolate, read_curve, seed_points, evaluate_curve, curve_document
from project_paths import PROJECT_ROOT


class CurveLibraryTests(unittest.TestCase):
    def test_catalog_data_and_derived_curve_roundtrip(self):
        with tempfile.TemporaryDirectory() as root:
            library = CurveLibrary(root, PROJECT_ROOT)
            entries = library.catalog()['entries']
            self.assertEqual(len(entries), 4)
            base = next(e for e in entries if e['id'] == 'builtin:cod_linear_legacy.json')
            target = next(e for e in entries if e['id'] == 'builtin:cod_dynamic_legacy.json')
            self.assertEqual(base['points'], seed_points('cod_linear_legacy_lut', PROJECT_ROOT))
            c = compose_curves(base, target, '测试换算')
            self.assertGreater(len(c['points']), 32)
            self.assertAlmostEqual(interpolate(c['points'], .5), .461432846, places=7)
            path = library.create(c['name'], c['points'], c['source'])
            saved = read_curve(path)
            self.assertEqual(saved, c)
            for i in range(10001):
                x = i / 10000
                self.assertAlmostEqual(interpolate(base['points'], interpolate(saved['points'], x)), interpolate(target['points'], x), places=11)
            library.update(path.name, {**saved, 'name':'重命名'})
            self.assertEqual(read_curve(path)['name'], '重命名')
            library.archive(path.name)
            self.assertFalse(path.exists())
            self.assertEqual(len(list((Path(root)/'profiles/curves/archive').glob('*.json'))), 1)
            self.assertEqual(len(library.catalog()['entries']), 4)
            for identifier in ('../x.json', 'builtin:linear.json'):
                with self.assertRaises(ValueError): library.update(identifier, saved)

    def test_independent_arbitrary_monotone_curve_pairs(self):
        for seed in (7841, 99001, 12673):
            rng = random.Random(seed)
            for case in range(16):
                def curve(name):
                    # Spaced input knots and positive random response increments.
                    weights = [rng.uniform(.1, 1) for _ in range(8)]
                    total = sum(weights); ys = [0.]
                    for w in weights: ys.append(ys[-1] + w/total)
                    ys[-1] = 1.
                    return {'name':name,'points':[[i/8, y] for i,y in enumerate(ys)]}
                a, b = curve('A'), curve('B')
                c = compose_curves(a,b,'C')
                for i in range(1001):
                    x=i/1000
                    self.assertAlmostEqual(interpolate(a['points'], interpolate(c['points'], x)), interpolate(b['points'], x), places=10)

    def test_import_keeps_source_and_rejects_invalid_curve(self):
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'import.json'
            source=PROJECT_ROOT/'resources/curves/cod_linear_legacy.json'
            path.write_bytes(source.read_bytes())
            self.assertIn('url', read_curve(path)['source'])
            data=json.loads(path.read_text(encoding='utf-8'));data['points'][2][1]=0
            path.write_text(json.dumps(data),encoding='utf-8')
            with self.assertRaises(ValueError):read_curve(path)

    def test_library_deadzone_compensation_is_self_contained(self):
        standard = read_curve(PROJECT_ROOT/'resources/curves/cod_standard_legacy.json')
        identity = curve_document('线性 1:1', [[0,0],[1,1]])
        with tempfile.TemporaryDirectory() as root:
            library = CurveLibrary(root, PROJECT_ROOT)
            for seed in (911, 13791, 710393):
                rng = random.Random(seed)
                for i in range(12):
                    base = dict(standard, deadzone=dict(start=rng.uniform(.1,.3), end=rng.uniform(0,.2)))
                    target = dict(identity, deadzone=dict(start=rng.uniform(0,.2), end=rng.uniform(0,.1))) if i%2 else identity
                    result = compose_curves(base,target,'标准含死区 → 线性')
                    saved = read_curve(library.create(result['name'], result['points'], result['source'], result))
                    self.assertEqual(saved, result)
                    self.assertEqual(saved['schema_version'], 2)
                    self.assertEqual(saved['source']['base_deadzone'],base['deadzone'])
                    self.assertEqual(evaluate_curve(saved,0),0)
                    self.assertGreater(evaluate_curve(saved,(target.get('deadzone',{}).get('start',0)+1.e-5)),base['deadzone']['start'])
                    for j in range(1001):
                        x=j/1000
                        self.assertAlmostEqual(evaluate_curve(base,evaluate_curve(saved,x)),evaluate_curve(target,x),places=10)
            base = dict(identity, deadzone=dict(start=.2,end=.1))
            result = compose_curves(base,identity,'补偿')
            self.assertAlmostEqual(evaluate_curve(result,.5),.55)
            self.assertAlmostEqual(evaluate_curve(result,1),.9)
            # A prepared result cannot produce responses outside its range.
            with self.assertRaisesRegex(ValueError,'超出'): compose_curves(result,identity,'不可达')
            for bad in (dict(start=.9,end=.1), dict(start=-.1), dict(start=float('nan'))):
                with self.assertRaises(ValueError): curve_document('无效',identity['points'],dict(deadzone=bad))
