"""Typed activation shared by the Python controller and vision adapters.

Physical ADS, manual fire and application requests are distinct sources.
Search demand and AI permission are projections, never stored boolean mirrors.
"""
from enum import IntEnum, StrEnum


class SearchRequest(StrEnum):
    IDLE = "idle"
    DETECTION_ONLY = "detection_only"
    ASSIST_SEARCH = "assist_search"


class AimActivation(IntEnum):
    OFF = 0
    PHYSICAL_ADS = 1
    MANUAL_FIRE = 2
    ADS_AND_FIRE = 3
    APPLICATION = 4

    @classmethod
    def from_inputs(cls, *, physical_ads: bool, manual_fire: bool = False):
        if physical_ads:
            return cls.ADS_AND_FIRE if manual_fire else cls.PHYSICAL_ADS
        return cls.MANUAL_FIRE if manual_fire else cls.OFF

    @property
    def permits_assist(self) -> bool:
        return self is not AimActivation.OFF

    @property
    def physical_ads(self) -> bool:
        return self in (AimActivation.PHYSICAL_ADS, AimActivation.ADS_AND_FIRE)

    @property
    def search_request(self) -> SearchRequest:
        return SearchRequest.ASSIST_SEARCH if self.permits_assist else SearchRequest.IDLE
