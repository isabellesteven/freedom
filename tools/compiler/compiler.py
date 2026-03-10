import binascii
import struct
from dataclasses import dataclass
from typing import Dict, List, Tuple

from .dsl_parser import Graph, IoDecl, parse_dsl


SECT_REQUIRES = 1
SECT_HEAPS = 2
SECT_BUFFERS = 3
SECT_NODES = 4
SECT_SCHEDULE = 5
SECT_PARAM_DEFAULTS = 6
SECT_GRAPH_CONFIG = 8

FMT_MAP = {
    "f32": 1,
    "s16": 2,
}
FMT_BYTES = {
    "f32": 4,
    "s16": 2,
}
MODULE_ID = {
    "Gain": 0x00001001,
}


@dataclass
class CompiledBlob:
    blob: bytes


def _u16(v: int) -> bytes:
    return struct.pack("<H", v)


def _u32(v: int) -> bytes:
    return struct.pack("<I", v)


def _u64(v: int) -> bytes:
    return struct.pack("<Q", v)


def _f32(v: float) -> bytes:
    return struct.pack("<f", v)


def _pad_to(v: int, align: int) -> int:
    if align <= 1:
        return v
    return (v + align - 1) & ~(align - 1)


def _section(sect_type: int, payload: bytes) -> bytes:
    hdr = _u32(sect_type) + _u32(len(payload)) + _u32(0) + _u32(0)
    return hdr + payload


def _pick_io(graph: Graph, direction: str) -> IoDecl:
    for io in graph.ios:
        if io.direction == direction:
            return io
    raise ValueError(f"missing {direction} io")


def compile_graph(graph: Graph) -> CompiledBlob:
    # v1 compiler currently targets the simple chain model used by gain_chain.
    io_in = _pick_io(graph, "input")
    io_out = _pick_io(graph, "output")

    if io_in.sample_fmt != io_out.sample_fmt:
        raise ValueError("input/output format mismatch")
    if io_in.sample_rate_khz != io_out.sample_rate_khz:
        raise ValueError("input/output sample rate mismatch")
    if io_in.block != io_out.block or io_in.channels != io_out.channels:
        raise ValueError("input/output shape mismatch")

    if len(graph.nodes) != 1:
        raise ValueError("v1 compiler currently supports exactly one node")

    node = graph.nodes[0]
    if node.module not in MODULE_ID:
        raise ValueError(f"unsupported module: {node.module}")

    # Heaps and buffers (same sizing convention as disasm spec example).
    block_bytes = io_in.block * io_in.channels * FMT_BYTES[io_in.sample_fmt]
    buf_slots = 2
    buf1_total = block_bytes * buf_slots
    buf2_offset = _pad_to(buf1_total, 16)
    buf2_total = block_bytes * buf_slots
    heap_io_bytes = _pad_to(buf2_offset + buf2_total, 16)

    # REQUIRES
    requires = bytearray()
    requires += _u32(1)
    requires += _u32(MODULE_ID[node.module])
    requires += _u16(1) + _u16(0)
    requires += _u32(0)

    # GRAPH_CONFIG
    graph_config = bytearray()
    graph_config += _u32(io_in.sample_rate_khz * 1000)
    graph_config += _u32(1)

    # HEAPS
    heaps = bytearray()
    heaps += _u32(3)
    heaps += _u32(1) + _u32(4) + _u32(heap_io_bytes) + _u32(16)
    heaps += _u32(2) + _u32(3) + _u32(256) + _u32(16)
    heaps += _u32(3) + _u32(2) + _u32(256) + _u32(16)

    # BUFFERS
    buffers = bytearray()
    buffers += _u32(2)
    fmt = FMT_MAP[io_in.sample_fmt]
    for buf_id, off in [(1, 0), (2, buf2_offset)]:
        buffers += _u32(buf_id)
        buffers += struct.pack("<BBH", 0, fmt, 0)  # OWNED, fmt, flags
        buffers += _u32(1)  # heap io
        buffers += _u32(off)
        buffers += _u32(block_bytes)
        buffers += _u16(buf_slots)
        buffers += _u16(0)
        buffers += _u32(0)
        buffers += _u16(io_in.channels)
        buffers += _u16(io_in.block)

    # Node IDs stable as 10,20,...
    node_id = 10

    gain_db = node.params.get("gain_db", 0.0)
    if not isinstance(gain_db, (int, float)):
        raise ValueError("Gain.gain_db must be numeric")
    gain_db_f = float(gain_db)

    init = _u32(1) + _f32(gain_db_f)

    # NODES
    nodes = bytearray()
    nodes += _u32(1)
    nodes += _u32(node_id)
    nodes += _u32(MODULE_ID[node.module])
    nodes += _u32(2)  # state heap
    nodes += _u32(0)  # state offset (v1-B runtime allocated)
    nodes += _u32(16)
    nodes += _u32(16)
    nodes += _u32(len(init))
    nodes += _u32(4)
    nodes += init

    # SCHEDULE: single CALL in=[1], out=[2]
    schedule = bytearray()
    schedule += _u32(1)
    schedule += struct.pack("<BBBB", 1, 1, 1, 0)
    schedule += _u32(node_id)
    schedule += _u32(1)
    schedule += _u32(2)

    # PARAM_DEFAULTS
    param_defaults = bytearray()
    param_defaults += _u32(1)
    param_defaults += _u32(node_id)
    param_defaults += _u32(4)
    param_defaults += _f32(gain_db_f)

    sections = (
        _section(SECT_REQUIRES, bytes(requires))
        + _section(SECT_GRAPH_CONFIG, bytes(graph_config))
        + _section(SECT_HEAPS, bytes(heaps))
        + _section(SECT_BUFFERS, bytes(buffers))
        + _section(SECT_NODES, bytes(nodes))
        + _section(SECT_SCHEDULE, bytes(schedule))
        + _section(SECT_PARAM_DEFAULTS, bytes(param_defaults))
    )

    header = bytearray()
    header += b"GRPH"
    header += struct.pack("<BB", 1, 0)
    header += _u16(32)
    header += _u32(0)  # patched file bytes
    header += _u32(0x414E4350)  # PCNA
    header += _u64(1)
    header += _u64(0)

    blob_wo_crc = bytes(header) + sections
    file_bytes = len(blob_wo_crc) + 4
    header[8:12] = _u32(file_bytes)
    blob_wo_crc = bytes(header) + sections
    crc = binascii.crc32(blob_wo_crc) & 0xFFFFFFFF
    blob = blob_wo_crc + _u32(crc)

    return CompiledBlob(blob=blob)


def compile_dsl_to_blob(dsl_text: str) -> bytes:
    graph = parse_dsl(dsl_text)
    return compile_graph(graph).blob
