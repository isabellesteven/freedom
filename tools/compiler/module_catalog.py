"""Compiler-side module catalog.

This is the source of truth for modules the compiler knows how to lower. Module
specs include per-pin semantic properties so compatibility checks happen during
compilation rather than in the syntax parser.
"""

from dataclasses import dataclass
from enum import Enum
from typing import Dict, Optional, Tuple


class SignalType(str, Enum):
    FLOAT32 = "float32"
    FIXED32 = "fixed32"


@dataclass(frozen=True)
class PinSpec:
    name: str
    signal_type: SignalType
    channels: int


@dataclass(frozen=True)
class ModuleSpec:
    module_id: int
    inputs: Tuple[PinSpec, ...]
    outputs: Tuple[PinSpec, ...]
    state_bytes: int
    state_align: int
    caps: int
    allow_inplace_io0: bool = False

    def find_input(self, name: str) -> Optional[PinSpec]:
        for pin in self.inputs:
            if pin.name == name:
                return pin
        return None

    def find_output(self, name: str) -> Optional[PinSpec]:
        for pin in self.outputs:
            if pin.name == name:
                return pin
        return None


MODULE_CATALOG: Dict[str, ModuleSpec] = {
    "Gain": ModuleSpec(
        module_id=0x00001001,
        inputs=(PinSpec("in", SignalType.FLOAT32, 1),),
        outputs=(PinSpec("out", SignalType.FLOAT32, 1),),
        state_bytes=16,
        state_align=16,
        caps=0,
        allow_inplace_io0=True,
    ),
    "Sum2": ModuleSpec(
        module_id=0x00001002,
        inputs=(
            PinSpec("a", SignalType.FLOAT32, 1),
            PinSpec("b", SignalType.FLOAT32, 1),
        ),
        outputs=(PinSpec("out", SignalType.FLOAT32, 1),),
        state_bytes=4,
        state_align=4,
        caps=0,
    ),
    # Heterogeneous-pin examples used for compiler semantic validation.
    "ConstFixed": ModuleSpec(
        module_id=0x00001003,
        inputs=(),
        outputs=(PinSpec("out", SignalType.FIXED32, 1),),
        state_bytes=4,
        state_align=4,
        caps=0,
    ),
    "StereoControlMix": ModuleSpec(
        module_id=0x00001004,
        inputs=(
            PinSpec("audio_in", SignalType.FLOAT32, 2),
            PinSpec("control_in", SignalType.FIXED32, 1),
        ),
        outputs=(PinSpec("audio_out", SignalType.FLOAT32, 2),),
        state_bytes=8,
        state_align=4,
        caps=0,
    ),
    "StereoPass": ModuleSpec(
        module_id=0x00001005,
        inputs=(PinSpec("in", SignalType.FLOAT32, 2),),
        outputs=(PinSpec("out", SignalType.FLOAT32, 2),),
        state_bytes=8,
        state_align=4,
        caps=0,
        allow_inplace_io0=True,
    ),
    "StereoSource": ModuleSpec(
        module_id=0x00001006,
        inputs=(),
        outputs=(PinSpec("out", SignalType.FLOAT32, 2),),
        state_bytes=8,
        state_align=4,
        caps=0,
    ),
    "StereoToMonoTap": ModuleSpec(
        module_id=0x00001007,
        inputs=(PinSpec("in", SignalType.FLOAT32, 2),),
        outputs=(PinSpec("out", SignalType.FLOAT32, 1),),
        state_bytes=8,
        state_align=4,
        caps=0,
    ),
}


def get_module_spec(module_name: str) -> Optional[ModuleSpec]:
    return MODULE_CATALOG.get(module_name)


def require_module_spec(module_name: str) -> ModuleSpec:
    spec = get_module_spec(module_name)
    if spec is None:
        raise ValueError(f"unsupported module: {module_name}")
    return spec
