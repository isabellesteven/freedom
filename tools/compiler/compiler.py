import binascii
import struct
from collections import deque
from dataclasses import dataclass
from typing import Dict, List, Mapping, Optional, Sequence, Tuple

from .dsl_parser import Endpoint, Graph, IoDecl, NodeDecl, parse_dsl


SECT_REQUIRES = 1
SECT_HEAPS = 2
SECT_BUFFERS = 3
SECT_NODES = 4
SECT_SCHEDULE = 5
SECT_PARAM_DEFAULTS = 6
SECT_GRAPH_CONFIG = 8

HEAP_IO_ID = 1
HEAP_STATE_ID = 2
HEAP_PARAM_ID = 3

BUFFER_TYPE_OWNED = 0
BUFFER_TYPE_ALIAS = 2

FMT_MAP = {
    "f32": 1,
    "s16": 2,
}
FMT_BYTES = {
    "f32": 4,
    "s16": 2,
}


@dataclass(frozen=True)
class ModuleSpec:
    module_id: int
    input_pins: Tuple[str, ...]
    output_pins: Tuple[str, ...]
    state_bytes: int
    state_align: int
    caps: int
    allow_inplace_io0: bool = False


MODULE_SPECS: Dict[str, ModuleSpec] = {
    "Gain": ModuleSpec(
        module_id=0x00001001,
        input_pins=("in",),
        output_pins=("out",),
        state_bytes=16,
        state_align=16,
        caps=0,
        allow_inplace_io0=True,
    ),
    "Sum2": ModuleSpec(
        module_id=0x00001002,
        input_pins=("a", "b"),
        output_pins=("out",),
        state_bytes=4,
        state_align=4,
        caps=0,
    ),
}


@dataclass(frozen=True)
class CompiledBlob:
    blob: bytes


@dataclass(frozen=True)
class NodeRecord:
    node_id: int
    module_id: int
    state_heap_id: int
    state_bytes: int
    state_align: int
    init_blob: bytes
    param_defaults: bytes


@dataclass(frozen=True)
class ScheduleRecord:
    op_index: int
    node_id: int
    input_ids: Tuple[int, ...]
    output_ids: Tuple[int, ...]


@dataclass(frozen=True)
class LoweredGraph:
    version_major: int
    version_minor: int
    abi_tag: str
    uuid_lo: int
    uuid_hi: int
    requires: Tuple[int, ...]
    sample_rate_hz: int
    block_multiple_n: int
    heaps: Tuple[Tuple[int, str, int, int], ...]
    buffers: Tuple["BufferRecord", ...]
    nodes: Tuple[NodeRecord, ...]
    schedule: Tuple[ScheduleRecord, ...]


@dataclass(frozen=True)
class ResolvedSource:
    kind: str
    owner: str
    pin: str


@dataclass(frozen=True)
class ResolvedDest:
    kind: str
    owner: str
    pin: str


@dataclass(frozen=True)
class BufferRecord:
    buffer_id: int
    buffer_type: int
    alias_of: int
    offset_bytes: int
    size_bytes: int
    channels: int
    frames: int
    fmt: int
    slots: int


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


def _buf_type_name(buf_type: int) -> str:
    if buf_type == BUFFER_TYPE_OWNED:
        return "OWNED"
    if buf_type == BUFFER_TYPE_ALIAS:
        return "ALIAS"
    return "UNK"


def _fmt_name(fmt: int) -> str:
    if fmt == 1:
        return "F32"
    if fmt == 2:
        return "S16"
    return "UNK"


def _resolve_source(endpoint: Endpoint, ios: Mapping[str, IoDecl],
                    nodes: Mapping[str, NodeDecl]) -> ResolvedSource:
    if endpoint.name in ios:
        io = ios[endpoint.name]
        if io.direction != "input":
            raise ValueError(f"{endpoint.name} is not a graph input")
        if endpoint.pin is not None:
            raise ValueError(f"graph input {endpoint.name} does not take a pin")
        return ResolvedSource("io", endpoint.name, "out")
    if endpoint.name in nodes:
        node = nodes[endpoint.name]
        spec = MODULE_SPECS.get(node.module)
        if spec is None:
            raise ValueError(f"unsupported module: {node.module}")
        pin = endpoint.pin or (spec.output_pins[0] if len(spec.output_pins) == 1 else None)
        if pin is None or pin not in spec.output_pins:
            raise ValueError(f"invalid output pin {endpoint.name}.{endpoint.pin}")
        return ResolvedSource("node", endpoint.name, pin)
    raise ValueError(f"unknown source endpoint: {endpoint.name}")


def _resolve_dest(endpoint: Endpoint, ios: Mapping[str, IoDecl],
                  nodes: Mapping[str, NodeDecl]) -> ResolvedDest:
    if endpoint.name in ios:
        io = ios[endpoint.name]
        if io.direction != "output":
            raise ValueError(f"{endpoint.name} is not a graph output")
        if endpoint.pin is not None:
            raise ValueError(f"graph output {endpoint.name} does not take a pin")
        return ResolvedDest("io", endpoint.name, "in")
    if endpoint.name in nodes:
        node = nodes[endpoint.name]
        spec = MODULE_SPECS.get(node.module)
        if spec is None:
            raise ValueError(f"unsupported module: {node.module}")
        pin = endpoint.pin or (spec.input_pins[0] if len(spec.input_pins) == 1 else None)
        if pin is None or pin not in spec.input_pins:
            raise ValueError(f"invalid input pin {endpoint.name}.{endpoint.pin}")
        return ResolvedDest("node", endpoint.name, pin)
    raise ValueError(f"unknown destination endpoint: {endpoint.name}")


def _validate_graph_shape(graph: Graph, inputs: Sequence[IoDecl], outputs: Sequence[IoDecl]) -> None:
    if not inputs:
        raise ValueError("graph must declare at least one input")
    if not outputs:
        raise ValueError("graph must declare at least one output")

    ref = inputs[0]
    for io in list(inputs[1:]) + list(outputs):
        if io.sample_fmt != ref.sample_fmt:
            raise ValueError("all graph io must use the same sample format")
        if io.sample_rate_khz != ref.sample_rate_khz:
            raise ValueError("all graph io must use the same sample rate")
        if io.block != ref.block or io.channels != ref.channels:
            raise ValueError("all graph io must use the same block/channels")

    # Temporary compiler limit: one block shape per graph keeps runtime buffer
    # layout simple until typed per-edge formats are introduced.
    if ref.sample_fmt not in FMT_MAP:
        raise ValueError(f"unsupported sample format: {ref.sample_fmt}")


def _graph_maps(graph: Graph) -> Tuple[Dict[str, IoDecl], Dict[str, NodeDecl]]:
    ios_by_name: Dict[str, IoDecl] = {}
    nodes_by_name: Dict[str, NodeDecl] = {}

    for io in graph.ios:
        if io.name in ios_by_name or io.name in nodes_by_name:
            raise ValueError(f"duplicate graph symbol: {io.name}")
        ios_by_name[io.name] = io
    for node in graph.nodes:
        if node.name in ios_by_name or node.name in nodes_by_name:
            raise ValueError(f"duplicate graph symbol: {node.name}")
        nodes_by_name[node.name] = node
    return ios_by_name, nodes_by_name


def _topological_order(graph: Graph, deps: Mapping[str, List[str]]) -> List[str]:
    indegree: Dict[str, int] = {node.name: 0 for node in graph.nodes}
    adjacency: Dict[str, List[str]] = {node.name: [] for node in graph.nodes}
    declaration_order = {node.name: idx for idx, node in enumerate(graph.nodes)}

    for dst_name, src_names in deps.items():
        for src_name in src_names:
            adjacency[src_name].append(dst_name)
            indegree[dst_name] += 1

    ready = deque(sorted((name for name, deg in indegree.items() if deg == 0),
                         key=lambda name: declaration_order[name]))
    order: List[str] = []
    while ready:
        name = ready.popleft()
        order.append(name)
        for dst_name in sorted(adjacency[name], key=lambda item: declaration_order[item]):
            indegree[dst_name] -= 1
            if indegree[dst_name] == 0:
                ready.append(dst_name)

    if len(order) != len(graph.nodes):
        raise ValueError("graph contains a cycle")
    return order


def _gain_init_and_defaults(node: NodeDecl) -> Tuple[bytes, bytes]:
    gain_db = node.params.get("gain_db", 0.0)
    if not isinstance(gain_db, (int, float)):
        raise ValueError("Gain.gain_db must be numeric")
    gain_db_f = float(gain_db)
    return _u32(1) + _f32(gain_db_f), _f32(gain_db_f)


def _module_init_and_defaults(node: NodeDecl) -> Tuple[bytes, bytes]:
    if node.module == "Gain":
        return _gain_init_and_defaults(node)
    if node.module == "Sum2":
        if node.params:
            raise ValueError("Sum2 does not accept parameters")
        return b"", b""
    raise ValueError(f"unsupported module: {node.module}")


def _build_graph_connections(graph: Graph) -> Tuple[
        Dict[Tuple[str, str], ResolvedSource],
        Dict[str, ResolvedSource],
        Dict[ResolvedSource, int],
        Dict[str, List[str]]]:
    ios_by_name, nodes_by_name = _graph_maps(graph)
    node_inputs: Dict[Tuple[str, str], ResolvedSource] = {}
    graph_outputs: Dict[str, ResolvedSource] = {}
    consumer_count: Dict[ResolvedSource, int] = {}
    deps: Dict[str, List[str]] = {node.name: [] for node in graph.nodes}

    for conn in graph.connects:
        src = _resolve_source(conn.src, ios_by_name, nodes_by_name)
        dst = _resolve_dest(conn.dst, ios_by_name, nodes_by_name)

        if dst.kind == "node":
            key = (dst.owner, dst.pin)
            if key in node_inputs:
                raise ValueError(f"node input already connected: {dst.owner}.{dst.pin}")
            node_inputs[key] = src
            if src.kind == "node":
                deps[dst.owner].append(src.owner)
        else:
            if dst.owner in graph_outputs:
                raise ValueError(f"graph output already connected: {dst.owner}")
            graph_outputs[dst.owner] = src

        consumer_count[src] = consumer_count.get(src, 0) + 1

    for node in graph.nodes:
        spec = MODULE_SPECS.get(node.module)
        if spec is None:
            raise ValueError(f"unsupported module: {node.module}")
        for pin in spec.input_pins:
            if (node.name, pin) not in node_inputs:
                raise ValueError(f"missing input: {node.name}.{pin}")

    for io in graph.ios:
        if io.direction == "output" and io.name not in graph_outputs:
            raise ValueError(f"graph output is unconnected: {io.name}")

    return node_inputs, graph_outputs, consumer_count, deps


def _render_param_defaults(data: bytes) -> str:
    if len(data) == 4:
        value = struct.unpack("<f", data)[0]
        return f"f32({value:.1f})"
    return f"hex({data.hex().upper()})"


def _render_canonical_ir(lowered: LoweredGraph) -> str:
    lines: List[str] = []
    uuid = (
        f"{(lowered.uuid_hi >> 32) & 0xFFFFFFFF:08x}-"
        f"{(lowered.uuid_hi >> 16) & 0xFFFF:04x}-"
        f"{lowered.uuid_hi & 0xFFFF:04x}-"
        f"{(lowered.uuid_lo >> 48) & 0xFFFF:04x}-"
        f"{lowered.uuid_lo & 0xFFFFFFFFFFFF:012x}"
    )

    lines.append(
        f"GRPH v{lowered.version_major}.{lowered.version_minor} "
        f"abi={lowered.abi_tag} uuid={uuid}"
    )
    lines.append("Sections: REQUIRES GRAPH_CONFIG HEAPS BUFFERS NODES SCHEDULE PARAM_DEFAULTS")
    lines.append("")
    lines.append("[REQUIRES]")
    lines.append(f"count={len(lowered.requires)}")
    for module_id in lowered.requires:
        lines.append(f"module 0x{module_id:08X} ver=1.0 caps=0x00000000")
    lines.append("")
    lines.append("[GRAPH_CONFIG]")
    lines.append(f"sample_rate_hz={lowered.sample_rate_hz}")
    lines.append(f"block_multiple_N={lowered.block_multiple_n}")
    lines.append("")
    lines.append("[HEAPS]")
    lines.append(f"count={len(lowered.heaps)}")
    for heap_id, kind, heap_bytes, heap_align in lowered.heaps:
        lines.append(
            f"heap id={heap_id} kind={kind} bytes={heap_bytes} align={heap_align}"
        )
    lines.append("")
    lines.append("[BUFFERS]")
    lines.append(f"count={len(lowered.buffers)}")
    for record in lowered.buffers:
        lines.append(
            f"buf id={record.buffer_id} type={_buf_type_name(record.buffer_type)} "
            f"alias_of={record.alias_of} fmt={_fmt_name(record.fmt)} heap={HEAP_IO_ID} "
            f"off={record.offset_bytes} size={record.size_bytes} slots={record.slots} "
            f"stride=0 base=0 ch={record.channels} frames={record.frames}"
        )
    lines.append("")
    lines.append("[NODES]")
    lines.append(f"count={len(lowered.nodes)}")
    for node in lowered.nodes:
        lines.append(
            f"node id={node.node_id} module=0x{node.module_id:08X} "
            f"state_heap={node.state_heap_id} state_bytes={node.state_bytes} "
            f"align={node.state_align} init_bytes={len(node.init_blob)} "
            f"param_block_bytes={len(node.param_defaults)}"
        )
    lines.append("")
    lines.append("[SCHEDULE]")
    lines.append(f"op_count={len(lowered.schedule)}")
    for entry in lowered.schedule:
        in_ids = " ".join(str(buf_id) for buf_id in entry.input_ids)
        out_ids = " ".join(str(buf_id) for buf_id in entry.output_ids)
        lines.append(f"{entry.op_index}: CALL node={entry.node_id} in=[{in_ids}] out=[{out_ids}]")
    lines.append("")
    lines.append("[PARAM_DEFAULTS]")
    param_nodes = [node for node in lowered.nodes if node.param_defaults]
    lines.append(f"count={len(param_nodes)}")
    for node in param_nodes:
        lines.append(
            f"node={node.node_id} bytes={len(node.param_defaults)} "
            f"data={_render_param_defaults(node.param_defaults)}"
        )
    lines.append("")
    lines.append("[CRC32]")
    lines.append("status=ok")
    lines.append("")
    return "\n".join(lines)


def _lower_graph(graph: Graph) -> LoweredGraph:
    _graph_maps(graph)
    inputs = [io for io in graph.ios if io.direction == "input"]
    outputs = [io for io in graph.ios if io.direction == "output"]
    _validate_graph_shape(graph, inputs, outputs)

    node_inputs, graph_outputs, consumer_count, deps = _build_graph_connections(graph)
    schedule_order = _topological_order(graph, deps)

    io_shape = inputs[0]
    block_bytes = io_shape.block * io_shape.channels * FMT_BYTES[io_shape.sample_fmt]
    fmt = FMT_MAP[io_shape.sample_fmt]
    buf_slots = 2
    next_offset = 0
    node_id_by_name = {node.name: 10 + (index * 10) for index, node in enumerate(graph.nodes)}
    node_by_name = {node.name: node for node in graph.nodes}

    buffers: List[BufferRecord] = []
    signal_buffer_id: Dict[ResolvedSource, int] = {}
    physical_buffer: Dict[int, BufferRecord] = {}

    def allocate_buffer(buffer_type: int, alias_buffer_id: Optional[int] = None) -> int:
        nonlocal next_offset
        buffer_id = len(buffers) + 1
        if alias_buffer_id is None:
            offset = next_offset
            next_offset = _pad_to(next_offset + (block_bytes * buf_slots), 16)
        else:
            offset = physical_buffer[alias_buffer_id].offset_bytes
        record = BufferRecord(
            buffer_id=buffer_id,
            buffer_type=buffer_type,
            alias_of=alias_buffer_id or 0,
            offset_bytes=offset,
            size_bytes=block_bytes,
            channels=io_shape.channels,
            frames=io_shape.block,
            fmt=fmt,
            slots=buf_slots,
        )
        buffers.append(record)
        physical_buffer[buffer_id] = record
        return buffer_id

    for io in inputs:
        source = ResolvedSource("io", io.name, "out")
        signal_buffer_id[source] = allocate_buffer(BUFFER_TYPE_OWNED)

    node_records: List[NodeRecord] = []
    node_schedule: List[ScheduleRecord] = []

    for op_index, node_name in enumerate(schedule_order):
        node = node_by_name[node_name]
        spec = MODULE_SPECS[node.module]
        input_buffer_ids: List[int] = []
        for pin in spec.input_pins:
            input_source = node_inputs[(node.name, pin)]
            input_buffer_ids.append(signal_buffer_id[input_source])

        output_buffer_ids: List[int] = []
        for output_index, pin in enumerate(spec.output_pins):
            source = ResolvedSource("node", node.name, pin)
            alias_buffer_id = None
            if (
                spec.allow_inplace_io0
                and output_index == 0
                and len(input_buffer_ids) > 0
                and consumer_count[node_inputs[(node.name, spec.input_pins[0])]] == 1
            ):
                alias_buffer_id = input_buffer_ids[0]
            buffer_type = BUFFER_TYPE_ALIAS if alias_buffer_id is not None else BUFFER_TYPE_OWNED
            buffer_id = allocate_buffer(buffer_type, alias_buffer_id)
            signal_buffer_id[source] = buffer_id
            output_buffer_ids.append(buffer_id)

        init_bytes, param_defaults = _module_init_and_defaults(node)
        node_records.append(
            NodeRecord(
                node_id=node_id_by_name[node.name],
                module_id=spec.module_id,
                state_heap_id=HEAP_STATE_ID,
                state_bytes=spec.state_bytes,
                state_align=spec.state_align,
                init_blob=init_bytes,
                param_defaults=param_defaults,
            )
        )
        node_schedule.append(
            ScheduleRecord(
                op_index=op_index,
                node_id=node_id_by_name[node.name],
                input_ids=tuple(input_buffer_ids),
                output_ids=tuple(output_buffer_ids),
            )
        )

    for io in outputs:
        source = graph_outputs[io.name]
        source_buffer_id = signal_buffer_id[source]
        allocate_buffer(BUFFER_TYPE_ALIAS, source_buffer_id)

    used_specs = tuple(sorted({MODULE_SPECS[node.module].module_id for node in graph.nodes}))
    heaps = (
        (HEAP_IO_ID, "IO", _pad_to(next_offset, 16), 16),
        (HEAP_STATE_ID, "STATE", 256, 16),
        (HEAP_PARAM_ID, "PARAM", 256, 16),
    )

    return LoweredGraph(
        version_major=1,
        version_minor=0,
        abi_tag="PCNA",
        uuid_lo=1,
        uuid_hi=0,
        requires=used_specs,
        sample_rate_hz=io_shape.sample_rate_khz * 1000,
        block_multiple_n=1,
        heaps=heaps,
        buffers=tuple(buffers),
        nodes=tuple(node_records),
        schedule=tuple(node_schedule),
    )


def compile_graph(graph: Graph) -> CompiledBlob:
    lowered = _lower_graph(graph)

    requires = bytearray()
    requires += _u32(len(lowered.requires))
    for module_id in lowered.requires:
        requires += _u32(module_id)
        requires += _u16(1) + _u16(0)
        requires += _u32(0)

    graph_config = bytearray()
    graph_config += _u32(lowered.sample_rate_hz)
    graph_config += _u32(lowered.block_multiple_n)

    heaps = bytearray()
    heaps += _u32(len(lowered.heaps))
    for heap_id, kind, heap_bytes, heap_align in lowered.heaps:
        kind_value = {"SRAM": 0, "PSRAM": 1, "PARAM": 2, "STATE": 3, "IO": 4}[kind]
        heaps += _u32(heap_id) + _u32(kind_value) + _u32(heap_bytes) + _u32(heap_align)

    buffers_payload = bytearray()
    buffers_payload += _u32(len(lowered.buffers))
    for record in lowered.buffers:
        buffers_payload += _u32(record.buffer_id)
        buffers_payload += struct.pack("<BBH", record.buffer_type, record.fmt, 0)
        buffers_payload += _u32(HEAP_IO_ID)
        buffers_payload += _u32(record.offset_bytes)
        buffers_payload += _u32(record.size_bytes)
        buffers_payload += _u16(record.slots)
        buffers_payload += _u16(0)
        buffers_payload += _u32(record.alias_of)
        buffers_payload += _u16(record.channels)
        buffers_payload += _u16(record.frames)

    nodes_payload = bytearray()
    nodes_payload += _u32(len(lowered.nodes))
    param_defaults_payload = bytearray()
    param_defaults_entries = 0
    for node in lowered.nodes:
        nodes_payload += _u32(node.node_id)
        nodes_payload += _u32(node.module_id)
        nodes_payload += _u32(node.state_heap_id)
        nodes_payload += _u32(0)
        nodes_payload += _u32(node.state_bytes)
        nodes_payload += _u32(node.state_align)
        nodes_payload += _u32(len(node.init_blob))
        nodes_payload += _u32(len(node.param_defaults))
        nodes_payload += node.init_blob

        if node.param_defaults:
            param_defaults_entries += 1
            param_defaults_payload += _u32(node.node_id)
            param_defaults_payload += _u32(len(node.param_defaults))
            param_defaults_payload += node.param_defaults

    param_defaults_payload = _u32(param_defaults_entries) + param_defaults_payload

    schedule = bytearray()
    schedule += _u32(len(lowered.schedule))
    for entry in lowered.schedule:
        schedule += struct.pack("<BBBB", 1, len(entry.input_ids), len(entry.output_ids), 0)
        schedule += _u32(entry.node_id)
        for buffer_id in entry.input_ids:
            schedule += _u32(buffer_id)
        for buffer_id in entry.output_ids:
            schedule += _u32(buffer_id)

    sections = (
        _section(SECT_REQUIRES, bytes(requires))
        + _section(SECT_GRAPH_CONFIG, bytes(graph_config))
        + _section(SECT_HEAPS, bytes(heaps))
        + _section(SECT_BUFFERS, bytes(buffers_payload))
        + _section(SECT_NODES, bytes(nodes_payload))
        + _section(SECT_SCHEDULE, bytes(schedule))
        + _section(SECT_PARAM_DEFAULTS, bytes(param_defaults_payload))
    )

    header = bytearray()
    header += b"GRPH"
    header += struct.pack("<BB", 1, 0)
    header += _u16(32)
    header += _u32(0)
    header += _u32(0x414E4350)
    header += _u64(lowered.uuid_lo)
    header += _u64(lowered.uuid_hi)

    blob_wo_crc = bytes(header) + sections
    file_bytes = len(blob_wo_crc) + 4
    header[8:12] = _u32(file_bytes)
    blob_wo_crc = bytes(header) + sections
    crc = binascii.crc32(blob_wo_crc) & 0xFFFFFFFF
    blob = blob_wo_crc + _u32(crc)

    # Temporary compiler limits after this milestone:
    # - all graph io still share one format/rate/block shape
    # - all runtime buffers still live in the IO heap
    # - graph output mapping is implicit via trailing alias buffers in io order
    return CompiledBlob(blob=blob)


def compile_dsl_to_blob(dsl_text: str) -> bytes:
    graph = parse_dsl(dsl_text)
    return compile_graph(graph).blob


def compile_dsl_to_canonical_ir(dsl_text: str) -> str:
    graph = parse_dsl(dsl_text)
    return _render_canonical_ir(_lower_graph(graph))
