import pytest

from controllers.activation import AimActivation, SearchRequest
from controllers.base_controller import BaseController
from controllers.gamepad_controller import GamepadController
from controllers.kbm_controller import KBMController
from controllers.mouse_controller import MouseController
from tools.analyze_mouse_telemetry import _assist_active
from vision.runner import AdsAutoFireGate


@pytest.mark.parametrize("ads,fire,expected", [
    (False, False, AimActivation.OFF),
    (True, False, AimActivation.PHYSICAL_ADS),
    (False, True, AimActivation.MANUAL_FIRE),
    (True, True, AimActivation.ADS_AND_FIRE),
])
def test_sources_survive_projection(ads, fire, expected):
    state = AimActivation.from_inputs(physical_ads=ads, manual_fire=fire)
    assert state is expected
    assert state.physical_ads is ads
    assert state.permits_assist is (ads or fire)


@pytest.mark.parametrize("state", list(AimActivation))
def test_search_permission_does_not_grant_physical_ads_fire(state):
    gate = AdsAutoFireGate(0.12)
    gate.on_physical_ads(state.physical_ads, 1.0)
    assert gate.allow_auto_fire(True, 2.0) is state.physical_ads
    assert (state.search_request is SearchRequest.ASSIST_SEARCH) is state.permits_assist
    assert _assist_active(str(int(state))) is state.permits_assist


def test_application_transition_revokes_previous_ads_fire_readiness():
    gate = AdsAutoFireGate(0.12)
    gate.on_physical_ads(AimActivation.PHYSICAL_ADS.physical_ads, 1.0)
    assert gate.allow_auto_fire(True, 2.0)
    gate.on_physical_ads(AimActivation.APPLICATION.physical_ads, 2.01)
    assert not gate.allow_auto_fire(True, 2.01)


@pytest.mark.parametrize("controller", [BaseController, GamepadController, KBMController, MouseController])
def test_controller_interface_has_no_legacy_boolean_alias(controller):
    assert hasattr(controller, "aim_activation")
    assert not hasattr(controller, "is_" + "aiming")
