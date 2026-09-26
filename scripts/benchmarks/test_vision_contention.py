"""Negative controls: a fast candidate must not hide harm or missing evidence."""
import copy
import unittest
from run_vision_contention import assess, verify_signatures


def phase(index, variant='baseline', wall=2):
    return dict(phase=index, variant=variant, gpu_coverage=True, budget_pass=True,
        outputs_exact=True, clock_stable=True, external_quiet=True, load_valid=True,
        wall_ms=dict(mean=wall,p99=wall*2), load_wall_ms=dict(mean=3,p99=5),
        sm_mhz=dict(p50=2775), vision_gpu=dict(mean=10), actual_hz=60, load_hz=120,
        result_interval_ms=dict(p99=18))


class EligibilityTests(unittest.TestCase):
    def setUp(self):
        self.contract={'schedule':['baseline','candidate','candidate','baseline']}
        self.phases=[phase(0),phase(1,'candidate',1.8),phase(2,'candidate',1.8),phase(3)]

    def verdict(self):
        return assess(self.phases,self.contract)['status']

    def test_complete_matched_candidate_can_pass(self):
        self.assertEqual(self.verdict(),'OFFLINE_SCREEN_PASS')

    def test_speed_cannot_buy_competing_graphics_throughput(self):
        self.phases[1]['load_hz']=115
        self.assertEqual(self.verdict(),'CANDIDATE_REJECTED')

    def test_speed_cannot_buy_competing_graphics_tail(self):
        self.phases[1]['load_wall_ms']['p99']=6
        self.assertEqual(self.verdict(),'CANDIDATE_REJECTED')

    def test_speed_cannot_buy_stale_delivery(self):
        self.phases[1]['result_interval_ms']['p99']=25
        self.assertEqual(self.verdict(),'CANDIDATE_REJECTED')

    def test_evidence_and_correctness_are_hard_gates(self):
        for key in ('gpu_coverage','budget_pass','outputs_exact','clock_stable','external_quiet','load_valid'):
            with self.subTest(key=key):
                self.phases[1][key]=False
                self.assertEqual(self.verdict(),'INVALID')
                self.phases[1][key]=True

    def test_incomplete_abba_rejected(self):
        self.phases.pop()
        self.assertEqual(self.verdict(),'INVALID')

    def test_identical_candidate_not_called_improvement(self):
        self.phases[1]['wall_ms']=copy.deepcopy(self.phases[0]['wall_ms'])
        self.assertEqual(self.verdict(),'CANDIDATE_REJECTED')

    def test_baseline_drift_rejects_apparent_gain(self):
        self.phases[3]['wall_ms']['mean']=2.2
        self.assertEqual(self.verdict(),'INVALID')

    def test_cross_phase_dvfs_rejects_apparent_gain(self):
        self.phases[1]['sm_mhz']['p50']=2000
        self.assertEqual(self.verdict(),'INVALID')

    def test_late_corrupted_detection_rejected(self):
        records=[dict(frame=i,source=i%2,signature=str(i%2)) for i in range(6)]
        self.assertTrue(verify_signatures(records,6,2,{0:'0',1:'1'})[0])
        records[-1]['signature']='wrong-box'
        self.assertFalse(verify_signatures(records,6,2,{0:'0',1:'1'})[0])

    def test_missing_frame_rejected_even_with_all_sources(self):
        records=[dict(frame=i,source=i%2,signature=str(i%2)) for i in range(5)]
        self.assertFalse(verify_signatures(records,6,2,{0:'0',1:'1'})[0])

    def test_nan_cannot_bypass_comparisons(self):
        self.phases[1]['wall_ms']['mean']=float('nan')
        self.assertEqual(self.verdict(),'INVALID')


if __name__=='__main__':
    unittest.main()
