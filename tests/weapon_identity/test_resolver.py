import unittest

from vision.weapon_identity.adapters import get_adapter
from vision.weapon_identity.models import WeaponIdentityRecord
from vision.weapon_identity.signatures import SignatureMatch


class WeaponResolverTests(unittest.TestCase):
    def test_cod22_blueprint_alias_resolves_to_canonical_weapon_id(self):
        result = _resolve_weapon(
            adapter=get_adapter("cod22"),
            identity_records=(
                _weapon_record(
                    canonical_weapon_id="cod22-m4",
                    game="cod22",
                    display_name="M4",
                    alias_names=("M4A1",),
                    blueprint_names=("Blackcell Ember",),
                ),
            ),
            ranked_image_matches=(_match("sig-m4", "cod22-m4", 0.93),),
            text_candidates=("Blackcell Ember",),
            runtime_state=_runtime_state(),
            switch_suspected=False,
            timestamp="2026-05-05T18:00:00Z",
        )

        self.assertIsNotNone(result.event)
        self.assertEqual(result.event.canonical_weapon_id, "cod22-m4")
        self.assertEqual(result.event.source, "image+text")
        self.assertEqual(result.event.matched_name, "Blackcell Ember")
        self.assertFalse(result.event.degraded)
        self.assertEqual(result.runtime_state.confirmed_weapon_id, "cod22-m4")

    def test_cod21_text_window_confirmation_carries_forward_cached_weapon(self):
        initial = _resolve_weapon(
            adapter=get_adapter("cod21"),
            identity_records=(
                _weapon_record(
                    canonical_weapon_id="cod21-krig-c",
                    game="cod21",
                    display_name="Krig C",
                    alias_names=("KRIG C",),
                ),
            ),
            ranked_image_matches=(),
            text_candidates=("Krig C",),
            runtime_state=_runtime_state(),
            switch_suspected=True,
            timestamp="2026-05-05T18:00:00Z",
        )

        follow_up = _resolve_weapon(
            adapter=get_adapter("cod21"),
            identity_records=(
                _weapon_record(
                    canonical_weapon_id="cod21-krig-c",
                    game="cod21",
                    display_name="Krig C",
                    alias_names=("KRIG C",),
                ),
            ),
            ranked_image_matches=(),
            text_candidates=(),
            runtime_state=initial.runtime_state,
            switch_suspected=False,
            timestamp="2026-05-05T18:00:01Z",
        )

        self.assertIsNotNone(initial.event)
        self.assertEqual(initial.event.canonical_weapon_id, "cod21-krig-c")
        self.assertEqual(initial.event.source, "text")
        self.assertFalse(initial.event.degraded)

        self.assertIsNotNone(follow_up.event)
        self.assertEqual(follow_up.event.canonical_weapon_id, "cod21-krig-c")
        self.assertEqual(follow_up.event.source, "carry_forward")
        self.assertFalse(follow_up.event.degraded)
        self.assertEqual(follow_up.runtime_state.confirmed_weapon_id, "cod21-krig-c")

    def test_ambiguous_image_only_case_remains_degraded(self):
        result = _resolve_weapon(
            adapter=get_adapter("cod22"),
            identity_records=(
                _weapon_record(
                    canonical_weapon_id="cod22-m4",
                    game="cod22",
                    display_name="M4",
                ),
                _weapon_record(
                    canonical_weapon_id="cod22-kastov-762",
                    game="cod22",
                    display_name="Kastov 762",
                ),
            ),
            ranked_image_matches=(
                _match("sig-kastov", "cod22-kastov-762", 0.74),
                _match("sig-m4", "cod22-m4", 0.71),
            ),
            text_candidates=(),
            runtime_state=_runtime_state(confirmed_weapon_id="cod22-m4"),
            switch_suspected=False,
            timestamp="2026-05-05T18:00:02Z",
        )

        self.assertIsNotNone(result.event)
        self.assertEqual(result.event.canonical_weapon_id, "cod22-m4")
        self.assertEqual(result.event.source, "carry_forward")
        self.assertTrue(result.event.degraded)
        self.assertEqual(result.runtime_state.confirmed_weapon_id, "cod22-m4")

    def test_conflicting_image_and_text_signals_retain_previous_confirmed_weapon(self):
        result = _resolve_weapon(
            adapter=get_adapter("cod20"),
            identity_records=(
                _weapon_record(
                    canonical_weapon_id="cod20-m4",
                    game="cod20",
                    display_name="M4",
                ),
                _weapon_record(
                    canonical_weapon_id="cod20-kastov-762",
                    game="cod20",
                    display_name="Kastov 762",
                ),
                _weapon_record(
                    canonical_weapon_id="cod20-bruen-mk9",
                    game="cod20",
                    display_name="Bruen MK9",
                ),
            ),
            ranked_image_matches=(_match("sig-kastov", "cod20-kastov-762", 0.92),),
            text_candidates=("Bruen MK9",),
            runtime_state=_runtime_state(confirmed_weapon_id="cod20-m4"),
            switch_suspected=True,
            timestamp="2026-05-05T18:00:03Z",
        )

        self.assertIsNotNone(result.event)
        self.assertEqual(result.event.canonical_weapon_id, "cod20-m4")
        self.assertEqual(result.event.source, "carry_forward")
        self.assertTrue(result.event.degraded)
        self.assertEqual(result.runtime_state.confirmed_weapon_id, "cod20-m4")


def _resolve_weapon(**kwargs):
    from vision.weapon_identity.resolver import resolve_weapon

    return resolve_weapon(**kwargs)


def _runtime_state(*, confirmed_weapon_id=None):
    from vision.weapon_identity.runtime_state import ResolverRuntimeState

    return ResolverRuntimeState(confirmed_weapon_id=confirmed_weapon_id)


def _weapon_record(
    *,
    canonical_weapon_id,
    game,
    display_name,
    alias_names=(),
    blueprint_names=(),
):
    return WeaponIdentityRecord(
        canonical_weapon_id=canonical_weapon_id,
        game=game,
        weapon_family="assault_rifle",
        display_name=display_name,
        alias_names=alias_names,
        blueprint_names=blueprint_names,
        signature_refs=(),
        notes="",
        created_at="2026-05-05T17:55:00Z",
        updated_at="2026-05-05T17:55:00Z",
    )


def _match(signature_id, canonical_weapon_id, score):
    return SignatureMatch(
        signature_id=signature_id,
        canonical_weapon_id=canonical_weapon_id,
        score=score,
        template_score=score,
        edge_score=score,
        hash_score=score,
        structure_score=score,
    )


if __name__ == "__main__":
    unittest.main()
