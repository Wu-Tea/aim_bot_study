import unittest
from project_paths import PROJECT_ROOT
from desktop_app.runtime import RuntimeManager
from desktop_app.ads_geometry import admission,envelope,feedback,follow_example,acquisition_example,validate_policy


class NativeGeometryParityTests(unittest.TestCase):
    def test_low_speed_reaches_point_before_stopping(self):
        from desktop_app.ads_geometry import follow_response_example
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        for error in (30,10,3,2.01):
            actual=follow_response_example(policy,1000,1000,.8,.8,error,0,radius_px=150)
            self.assertGreater(actual['stick'][0],0)
        for error in (2,1,0,-1,-2):
            actual=follow_response_example(policy,1000,1000,.8,.8,error,0,radius_px=150)
            self.assertEqual(actual['stick'],(0,0)) if error==0 else self.assertGreater(actual['stick'][0]*error,0)
        before=follow_response_example(policy,40,40,.8,.8,1.999,0,radius_px=150)
        after=follow_response_example(policy,40,40,.8,.8,2.001,0,radius_px=150)
        self.assertLess(abs(after['stick'][0]-before['stick'][0]),.001)

    def test_small_distance_cannot_use_large_force_at_fastest_timing(self):
        from desktop_app.ads_geometry import follow_response_example
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        # 1.5 px is 1% of a 150 px range: sqrt(1%) of an 80% cap = 8%.
        result=follow_response_example(policy,5,5,.8,.8,1.5,0,radius_px=150)
        self.assertLessEqual(result['stick'][0],.080001)

    def test_near_center_position_does_not_prematurely_saturate(self):
        from desktop_app.ads_geometry import follow_response_example
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        result=follow_response_example(policy,80,80,.8,.8,32,0,radius_px=150)
        self.assertAlmostEqual(result['stick'][0],.8*(32/150)**.5,places=6)

    def test_independent_parameter_preview_matches_native_output(self):
        from desktop_app.ads_geometry import follow_response_example
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        self.assertEqual(policy['parameter_semantics'],'range-response-v3')
        self.assertEqual(len(policy['independent_examples']),864)
        for ref in policy['independent_examples']:
            with self.subTest(ref=ref):
                follow=follow_response_example(policy,ref['time'],ref['time']*.8,*ref['limits'],*ref['error'],radius_px=ref['radius'],minimum_stick=ref['minimum_stick'])
                ads=acquisition_example(policy,.25,*ref['limits'],*ref['error'],nominal_horizon=ref['time']/1000,output_limits=True,radius_px=ref['radius'],minimum_stick=ref['minimum_stick'])
                for actual,expected in zip(follow['stick'],ref['follow']):self.assertAlmostEqual(actual,expected,places=5)
                for actual,expected in zip(ads['stick'],ref['ads']):self.assertAlmostEqual(actual,expected,places=5)
    def test_ads_examples_match_native_size_direction_force_and_horizon_rules(self):
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        self.assertEqual(len(policy['ads_examples']),300)
        for ref in policy['ads_examples']:
            with self.subTest(ref=ref):
                actual=acquisition_example(policy,ref['size'],*ref['force'],*ref['error'],nominal_horizon=ref['horizon'])
                for left,right in zip(actual['stick'],ref['stick']):self.assertAlmostEqual(left,right,places=5)
        self.assertEqual(acquisition_example(policy,.25,0,0,30,-10)['stick'],(0,0))
        with self.assertRaises(ValueError):acquisition_example(policy,.25,1,1,float('nan'),0)
        above=acquisition_example(policy,.25,1,1,0,-5)['stick'][1]
        below=acquisition_example(policy,.25,1,1,0,5)['stick'][1]
        self.assertGreater(above,abs(below))
        small=acquisition_example(policy,.03,1,1,10,0)['stick'][0]
        large=acquisition_example(policy,.4,1,1,10,0)['stick'][0]
        self.assertGreater(large,small)

    def test_concrete_follow_examples_match_actual_native_joint_limits(self):
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        self.assertEqual(len(policy['follow_examples']),100)
        for ref in policy['follow_examples']:
            with self.subTest(ref=ref):
                actual=follow_example(policy,ref['distance'],*ref['force'],*ref['error'])
                for left,right in zip(actual['stick'],ref['stick']):self.assertAlmostEqual(left,right,places=5)
        self.assertEqual(follow_example(policy,1,0,0,30,-10)['stick'],(0,0))
        with self.assertRaises(ValueError):follow_example(policy,18,.3,.42,float('nan'),0)

    def test_one_pixel_is_effective_feedback_distance(self):
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        self.assertEqual(feedback(policy,1,.3)['distance'],1)
        self.assertAlmostEqual(feedback(policy,1,.3,error=.5)['demand'],.15,places=5)

    def test_preview_matches_production_geometry_for_size_and_shape_boundaries(self):
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        self.assertGreaterEqual(len(policy['references']),30)
        self.assertEqual(policy['release_samples'],3)
        for reference in policy['references']:
            with self.subTest(reference=reference):
                actual=envelope(policy,reference['width'],reference['height'],416,.4,.65,150)
                self.assertEqual(actual['wide_low'],reference['wide_low'])
                admitted=admission(policy,reference['width'],reference['height'],480,416)
                self.assertEqual(admitted['pickup'],reference['pickup_admitted'])
                self.assertEqual(admitted['tracking'],reference['tracking_admitted'])
                self.assertAlmostEqual(actual['radius'],reference['radius'],places=4)
                for left,right in zip(actual['aim'],reference['aim']):self.assertAlmostEqual(left,right,places=4)
                for left,right in zip(actual['region'],reference['region']):self.assertAlmostEqual(left,right,places=4)

    def test_preview_rejects_invalid_native_policy_and_dimensions(self):
        with self.assertRaises(ValueError):validate_policy({'schema_version':0})
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        for width,height,frame in ((0,20,416),(20,0,416),(20,20,0),(float('nan'),20,416)):
            with self.assertRaises(ValueError):envelope(policy,width,height,frame,.4,.65,150)

    def test_feedback_demands_match_native_solver_at_floor_force_and_horizon_boundaries(self):
        policy=RuntimeManager(PROJECT_ROOT).ads_geometry_policy()
        for reference in policy['feedback_references']:
            with self.subTest(reference=reference):
                actual=feedback(policy,reference['value'],reference['force'])
                self.assertAlmostEqual(actual['distance'],reference['distance'],places=4)
                self.assertAlmostEqual(actual['demand'],reference['position_demand'],places=5)
                self.assertAlmostEqual(actual['linear_output'],reference['linear_output'],places=5)
        for value in (1,8,12,18):self.assertEqual(feedback(policy,value,.3)['distance'],value)
        self.assertLess(feedback(policy,24,.3)['demand'],feedback(policy,8,.3)['demand'])
        self.assertEqual(feedback(policy,1,.3)['linear_output'],feedback(policy,18,.3)['linear_output'])
        self.assertAlmostEqual(feedback(policy,1,.8)['horizon'],.005,places=6)
        with self.assertRaises(ValueError):feedback(policy,0,.3)
