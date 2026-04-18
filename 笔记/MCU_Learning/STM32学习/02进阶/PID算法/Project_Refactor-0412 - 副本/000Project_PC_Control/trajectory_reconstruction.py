from __future__ import annotations

import argparse
import json
import math
import struct
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Optional

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import serial
import yaml
from scipy.optimize import least_squares

from motion_analysis import build_motion_summary


FRAME_STRUCT = struct.Struct("<BBBBIiiffBBhhBBB")
UINT32_WRAP = 1 << 32
MODE_STRAIGHT = "STRAIGHT"
MODE_TRACK = "TRACK"
MODE_NAMES = (MODE_STRAIGHT, MODE_TRACK)
MODE_COMMAND_SPECS = {
    MODE_STRAIGHT: {
        "config_key": "straight",
        "pid_entries": (("speed_pid", "SPEED"), ("angle_pid", "ANGLE")),
        "default_target_speed": 30.0,
    },
    MODE_TRACK: {
        "config_key": "track",
        "pid_entries": (("speed_pid", "SPEED"), ("line_pid", "LINE")),
        "default_target_speed": 18.0,
    },
}


def wrap_angle_rad(value: float) -> float:
    return math.atan2(math.sin(value), math.cos(value))


def mode_name_from_id(mode: int) -> str:
    if 0 <= int(mode) < len(MODE_NAMES):
        return MODE_NAMES[int(mode)]
    return MODE_STRAIGHT


def normalize_mode_name(value: Any) -> str:
    mode_name = str(value).upper()
    if mode_name not in MODE_COMMAND_SPECS:
        raise ValueError(f"Unsupported mode: {value}")
    return mode_name


def cfg_get(cfg: dict[str, Any], *keys: str, default: Any = None) -> Any:
    node: Any = cfg
    for key in keys:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def resolve_path(root: Path, value: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return (root / path).resolve()


@dataclass
class TelemetryFrame:
    mode: int
    flags: int
    timestamp_us: int
    encoder_left: int
    encoder_right: int
    yaw_deg: float
    gyro_z_dps: float
    line_bits: int
    line_aux: int
    pwm_left: int
    pwm_right: int

    @property
    def is_running(self) -> bool:
        return bool(self.flags & 0x01)

    @property
    def exp_active(self) -> bool:
        return bool(self.flags & 0x02)

    @property
    def imu_valid(self) -> bool:
        return bool(self.flags & 0x04)

    @property
    def line_valid(self) -> bool:
        return bool(self.flags & 0x08)


@dataclass
class MeasurementStep:
    index: int
    mode: int
    flags: int
    timestamp_us_raw: int
    timestamp_us_unwrapped: int
    timestamp_s: float
    dt_s: float
    encoder_left: int
    encoder_right: int
    delta_left_counts: int
    delta_right_counts: int
    distance_left_m: float
    distance_right_m: float
    linear_velocity_mps: float
    angular_velocity_radps: float
    yaw_deg: float
    yaw_rad: float
    gyro_z_dps: float
    gyro_z_radps: float
    line_bits: int
    line_count: int
    line_error_m: float
    pwm_left: int
    pwm_right: int
    slip_detected: bool
    imu_valid: bool
    line_valid: bool


class BinaryFrameParser:
    def __init__(self, head: list[int], tail: list[int], frame_size: int) -> None:
        self.head = bytes(head)
        self.tail = bytes(tail)
        self.frame_size = frame_size
        self.buffer = bytearray()
        self.frames_ok = 0
        self.frames_bad_checksum = 0
        self.frames_bad_tail = 0
        self.bytes_discarded = 0

    @staticmethod
    def checksum_xor(data: bytes) -> int:
        value = 0
        for byte in data:
            value ^= byte
        return value

    def push(self, data: bytes) -> list[TelemetryFrame]:
        self.buffer.extend(data)
        frames: list[TelemetryFrame] = []
        while len(self.buffer) >= self.frame_size:
            idx = self.buffer.find(self.head)
            if idx < 0:
                self.bytes_discarded += max(0, len(self.buffer) - 1)
                self.buffer = self.buffer[-1:]
                break
            if idx > 0:
                self.bytes_discarded += idx
                del self.buffer[:idx]
            if len(self.buffer) < self.frame_size:
                break
            raw = bytes(self.buffer[: self.frame_size])
            if raw[-2:] != self.tail:
                self.frames_bad_tail += 1
                self.bytes_discarded += 1
                del self.buffer[0]
                continue
            if raw[30] != self.checksum_xor(raw[2:30]):
                self.frames_bad_checksum += 1
                self.bytes_discarded += 1
                del self.buffer[0]
                continue
            values = FRAME_STRUCT.unpack(raw)
            frame = TelemetryFrame(
                mode=values[2],
                flags=values[3],
                timestamp_us=values[4],
                encoder_left=values[5],
                encoder_right=values[6],
                yaw_deg=values[7],
                gyro_z_dps=values[8],
                line_bits=values[9],
                line_aux=values[10],
                pwm_left=values[11],
                pwm_right=values[12],
            )
            frames.append(frame)
            self.frames_ok += 1
            del self.buffer[: self.frame_size]
        return frames


class TextFrameParser:
    """Parse text-based HB telemetry lines from the refactored firmware.

    Format: HB:t=<ms>,run=<0|1>,el=<delta>,er=<delta>,yaw=<deg>,yr=<dps>,
            pc=<core>,hd=<diff>,OL=<pwm>,OR=<pwm>,sb=<bits>,lp=<pos>
    """

    def __init__(self, mode: int = 0) -> None:
        self.buffer = ""
        self.frames_ok = 0
        self.frames_bad = 0
        self.bytes_discarded = 0
        self.cum_encoder_left = 0
        self.cum_encoder_right = 0
        self.mode = mode

    # Compatibility properties for build_summary
    frames_bad_checksum = 0
    frames_bad_tail = 0

    def push(self, data: bytes) -> list[TelemetryFrame]:
        self.buffer += data.decode("utf-8", errors="ignore")
        frames: list[TelemetryFrame] = []
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            line = line.strip()
            if not line.startswith("HB:"):
                continue
            frame = self._parse_hb(line)
            if frame is not None:
                frames.append(frame)
                self.frames_ok += 1
            else:
                self.frames_bad += 1
        return frames

    def _parse_hb(self, line: str) -> Optional[TelemetryFrame]:
        try:
            kv: dict[str, str] = {}
            for pair in line[3:].split(","):
                if "=" not in pair:
                    continue
                k, v = pair.split("=", 1)
                kv[k.strip()] = v.strip()

            el = int(kv.get("el", "0"))
            er = int(kv.get("er", "0"))
            self.cum_encoder_left += el
            self.cum_encoder_right += er

            # Parse mode from 'm' field if present
            m_str = kv.get("m", "").upper()
            if m_str == "T":
                mode_id = 1
            elif m_str == "S":
                mode_id = 0
            else:
                mode_id = self.mode

            run = int(kv.get("run", "0"))
            flags = run & 0x01
            flags |= 0x04  # imu_valid always set
            sb = int(kv.get("sb", "0"))
            if sb > 0:
                flags |= 0x08  # line_valid

            return TelemetryFrame(
                mode=mode_id,
                flags=flags,
                timestamp_us=int(kv.get("t", "0")) * 1000,
                encoder_left=self.cum_encoder_left,
                encoder_right=self.cum_encoder_right,
                yaw_deg=float(kv.get("yaw", "0")),
                gyro_z_dps=float(kv.get("yr", "0")),
                line_bits=sb,
                line_aux=int(kv.get("pc", "0")),  # reuse line_aux for pwmCore
                pwm_left=int(kv.get("OL", "0")),
                pwm_right=int(kv.get("OR", "0")),
            )
        except Exception:
            return None


class ReferencePath:
    def __init__(self, cfg: dict[str, Any], project_root: Path) -> None:
        self.enabled = bool(cfg_get(cfg, "reference_path", "enabled", default=False))
        self.path_type = str(cfg_get(cfg, "reference_path", "type", default="line")).lower()
        self.heading_rad = math.radians(float(cfg_get(cfg, "reference_path", "heading_deg", default=0.0)))
        self.offset_m = float(cfg_get(cfg, "reference_path", "offset_m", default=0.0))
        self.points: Optional[np.ndarray] = None
        if self.enabled and self.path_type == "polyline_csv":
            csv_path = str(cfg_get(cfg, "reference_path", "csv_path", default=""))
            if csv_path:
                path = resolve_path(project_root, csv_path)
                frame = pd.read_csv(path)
                self.points = frame[["x", "y"]].to_numpy(dtype=float)
                if len(self.points) < 2:
                    self.points = None
                    self.enabled = False
            else:
                self.enabled = False

    def signed_distance(self, x: float, y: float) -> Optional[float]:
        if not self.enabled:
            return None
        if self.path_type == "line":
            nx = -math.sin(self.heading_rad)
            ny = math.cos(self.heading_rad)
            return x * nx + y * ny - self.offset_m
        if self.path_type == "polyline_csv" and self.points is not None:
            best_distance = None
            best_signed = None
            point = np.array([x, y], dtype=float)
            for i in range(len(self.points) - 1):
                p0 = self.points[i]
                p1 = self.points[i + 1]
                seg = p1 - p0
                seg_norm_sq = float(np.dot(seg, seg))
                if seg_norm_sq <= 0.0:
                    continue
                t = float(np.dot(point - p0, seg) / seg_norm_sq)
                t = max(0.0, min(1.0, t))
                proj = p0 + t * seg
                diff = point - proj
                distance = float(np.linalg.norm(diff))
                if best_distance is None or distance < best_distance:
                    cross = seg[0] * diff[1] - seg[1] * diff[0]
                    best_distance = distance
                    best_signed = math.copysign(distance, cross if cross != 0.0 else 1.0)
            return best_signed
        return None

    def numerical_gradient(self, x: float, y: float, step: float = 1e-3) -> tuple[float, float]:
        d0 = self.signed_distance(x, y)
        if d0 is None:
            return 0.0, 0.0
        dx1 = self.signed_distance(x + step, y)
        dx0 = self.signed_distance(x - step, y)
        dy1 = self.signed_distance(x, y + step)
        dy0 = self.signed_distance(x, y - step)
        if dx1 is None or dx0 is None or dy1 is None or dy0 is None:
            return 0.0, 0.0
        return (dx1 - dx0) / (2.0 * step), (dy1 - dy0) / (2.0 * step)


class TrajectoryEKF:
    def __init__(self, cfg: dict[str, Any], reference_path: ReferencePath) -> None:
        self.cfg = cfg
        self.reference_path = reference_path
        initial_pose = cfg_get(cfg, "robot", "initial_pose", default=[0.0, 0.0, 0.0])
        self.x = np.zeros(6, dtype=float)
        self.x[0] = float(initial_pose[0])
        self.x[1] = float(initial_pose[1])
        self.x[2] = math.radians(float(initial_pose[2]))
        self.P = np.diag([0.05, 0.05, 0.1, 0.5, 0.5, 0.1]).astype(float)

    def _process_covariance(self, dt: float) -> np.ndarray:
        process = cfg_get(self.cfg, "ekf", "process_noise", default={})
        q_pos = float(process.get("position_m2", 0.0004))
        q_heading = float(process.get("heading_rad2", 0.0008))
        q_speed = float(process.get("speed_mps2", 0.04))
        q_omega = float(process.get("omega_radps2", 0.08))
        q_bias = float(process.get("gyro_bias_radps2", 0.0005))
        scale = max(dt, 1e-4)
        return np.diag([
            q_pos * scale,
            q_pos * scale,
            q_heading * scale,
            q_speed * scale,
            q_omega * scale,
            q_bias * scale,
        ])

    def predict(self, dt: float) -> None:
        if dt <= 0.0:
            return
        theta = float(self.x[2])
        v = float(self.x[3])
        omega = float(self.x[4])
        nx = self.x.copy()
        nx[0] += v * math.cos(theta) * dt
        nx[1] += v * math.sin(theta) * dt
        nx[2] = wrap_angle_rad(theta + omega * dt)
        f = np.eye(6, dtype=float)
        f[0, 2] = -v * math.sin(theta) * dt
        f[0, 3] = math.cos(theta) * dt
        f[1, 2] = v * math.cos(theta) * dt
        f[1, 3] = math.sin(theta) * dt
        f[2, 4] = dt
        self.x = nx
        self.P = f @ self.P @ f.T + self._process_covariance(dt)

    def update(self, z: np.ndarray, h: np.ndarray, h_jac: np.ndarray, r: np.ndarray, angle_index: Optional[int] = None) -> None:
        innovation = z - h
        if angle_index is not None:
            innovation[angle_index] = wrap_angle_rad(float(innovation[angle_index]))
        s = h_jac @ self.P @ h_jac.T + r
        k = self.P @ h_jac.T @ np.linalg.inv(s)
        self.x = self.x + k @ innovation
        self.x[2] = wrap_angle_rad(float(self.x[2]))
        identity = np.eye(self.P.shape[0], dtype=float)
        self.P = (identity - k @ h_jac) @ self.P

    def update_encoder(self, linear_velocity_mps: float, angular_velocity_radps: float, slip_detected: bool) -> None:
        variance_cfg = cfg_get(self.cfg, "ekf", "encoder_variance", default={})
        linear_var = float(variance_cfg.get("linear_mps2", 0.015))
        omega_var = float(variance_cfg.get("omega_radps2", 0.03))
        if slip_detected:
            scale = float(cfg_get(self.cfg, "ekf", "slip_encoder_variance_scale", default=5.0))
            linear_var *= scale
            omega_var *= scale
        z = np.array([linear_velocity_mps, angular_velocity_radps], dtype=float)
        h = np.array([self.x[3], self.x[4]], dtype=float)
        h_jac = np.zeros((2, 6), dtype=float)
        h_jac[0, 3] = 1.0
        h_jac[1, 4] = 1.0
        r = np.diag([linear_var, omega_var]).astype(float)
        self.update(z, h, h_jac, r)

    def update_yaw(self, yaw_rad: float) -> None:
        yaw_var = float(cfg_get(self.cfg, "ekf", "yaw_variance_rad2", default=0.0015))
        z = np.array([yaw_rad], dtype=float)
        h = np.array([self.x[2]], dtype=float)
        h_jac = np.zeros((1, 6), dtype=float)
        h_jac[0, 2] = 1.0
        r = np.array([[yaw_var]], dtype=float)
        self.update(z, h, h_jac, r, angle_index=0)

    def update_gyro(self, gyro_radps: float) -> None:
        gyro_var = float(cfg_get(self.cfg, "ekf", "gyro_variance_radps2", default=0.01))
        z = np.array([gyro_radps], dtype=float)
        h = np.array([self.x[4] + self.x[5]], dtype=float)
        h_jac = np.zeros((1, 6), dtype=float)
        h_jac[0, 4] = 1.0
        h_jac[0, 5] = 1.0
        r = np.array([[gyro_var]], dtype=float)
        self.update(z, h, h_jac, r)

    def update_line(self, line_error_m: float) -> None:
        if not self.reference_path.enabled:
            return
        expected = self.reference_path.signed_distance(float(self.x[0]), float(self.x[1]))
        if expected is None:
            return
        gx, gy = self.reference_path.numerical_gradient(float(self.x[0]), float(self.x[1]))
        h_jac = np.zeros((1, 6), dtype=float)
        h_jac[0, 0] = gx
        h_jac[0, 1] = gy
        r = np.array([[float(cfg_get(self.cfg, "line_sensor", "variance_m2", default=0.0004))]], dtype=float)
        z = np.array([line_error_m], dtype=float)
        h = np.array([expected], dtype=float)
        self.update(z, h, h_jac, r)


class SlidingWindowOptimizer:
    def __init__(self, cfg: dict[str, Any], reference_path: ReferencePath) -> None:
        self.cfg = cfg
        self.reference_path = reference_path

    def _propagate_state(self, state: np.ndarray, dt: float) -> np.ndarray:
        out = state.copy()
        out[0] += state[3] * math.cos(state[2]) * dt
        out[1] += state[3] * math.sin(state[2]) * dt
        out[2] = wrap_angle_rad(float(state[2] + state[4] * dt))
        return out

    def optimize(self, measurements: list[MeasurementStep], state_guesses: list[np.ndarray]) -> list[np.ndarray]:
        if not bool(cfg_get(self.cfg, "optimization", "enabled", default=True)):
            return state_guesses
        if len(measurements) < 3:
            return state_guesses
        motion_weight = float(cfg_get(self.cfg, "optimization", "motion_weight", default=1.0))
        yaw_weight = float(cfg_get(self.cfg, "optimization", "yaw_weight", default=1.0))
        gyro_weight = float(cfg_get(self.cfg, "optimization", "gyro_weight", default=1.0))
        speed_weight = float(cfg_get(self.cfg, "optimization", "speed_weight", default=0.5))
        line_weight = float(cfg_get(self.cfg, "optimization", "line_weight", default=0.5))
        prior_weight = 4.0
        initial = np.concatenate([state.copy() for state in state_guesses])
        first_state = state_guesses[0].copy()

        def residual(flat: np.ndarray) -> np.ndarray:
            states = flat.reshape((-1, 6))
            residuals: list[float] = []
            prior = states[0] - first_state
            prior[2] = wrap_angle_rad(float(prior[2]))
            residuals.extend((prior_weight * prior).tolist())
            for i in range(len(states) - 1):
                dt = measurements[i + 1].dt_s
                if dt <= 0.0:
                    continue
                pred = self._propagate_state(states[i], dt)
                motion = states[i + 1] - pred
                motion[2] = wrap_angle_rad(float(motion[2]))
                residuals.extend((motion_weight * motion).tolist())
            for i, measurement in enumerate(measurements):
                state = states[i]
                yaw_res = wrap_angle_rad(float(state[2] - measurement.yaw_rad))
                gyro_res = float(state[4] + state[5] - measurement.gyro_z_radps)
                residuals.append(yaw_weight * yaw_res)
                residuals.append(gyro_weight * gyro_res)
                if measurement.dt_s > 0.0:
                    residuals.append(speed_weight * float(state[3] - measurement.linear_velocity_mps))
                    residuals.append(speed_weight * float(state[4] - measurement.angular_velocity_radps))
                if self.reference_path.enabled and measurement.line_valid and measurement.line_count > 0:
                    expected = self.reference_path.signed_distance(float(state[0]), float(state[1]))
                    if expected is not None:
                        residuals.append(line_weight * float(expected - measurement.line_error_m))
            return np.asarray(residuals, dtype=float)

        result = least_squares(
            residual,
            initial,
            max_nfev=int(cfg_get(self.cfg, "optimization", "max_nfev", default=40)),
            method="trf",
        )
        optimized = []
        for row in result.x.reshape((-1, 6)):
            copy = row.copy()
            copy[2] = wrap_angle_rad(float(copy[2]))
            optimized.append(copy)
        return optimized


class LivePlotter:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled
        self.figure = None
        self.axes = None
        self.odom_line = None
        self.ekf_line = None
        self.opt_line = None
        if not self.enabled:
            return
        plt.ion()
        self.figure, self.axes = plt.subplots(figsize=(8, 8))
        self.odom_line, = self.axes.plot([], [], label="odom", linewidth=1.2)
        self.ekf_line, = self.axes.plot([], [], label="ekf", linewidth=1.6)
        self.opt_line, = self.axes.plot([], [], label="opt", linewidth=1.8)
        self.axes.set_aspect("equal", adjustable="box")
        self.axes.grid(True)
        self.axes.legend()

    def update(self, odom_xy: np.ndarray, ekf_xy: np.ndarray, opt_xy: np.ndarray) -> None:
        if not self.enabled or self.axes is None or self.figure is None:
            return
        if len(odom_xy) > 0:
            self.odom_line.set_data(odom_xy[:, 0], odom_xy[:, 1])
        if len(ekf_xy) > 0:
            self.ekf_line.set_data(ekf_xy[:, 0], ekf_xy[:, 1])
        if len(opt_xy) > 0:
            self.opt_line.set_data(opt_xy[:, 0], opt_xy[:, 1])
        self.axes.relim()
        self.axes.autoscale_view()
        self.figure.canvas.draw_idle()
        self.figure.canvas.flush_events()
        plt.pause(0.001)

    def save(self, path: Path) -> None:
        if self.figure is not None:
            self.figure.savefig(path, dpi=160, bbox_inches="tight")


class TrajectoryAnalyzer:
    def __init__(self, cfg: dict[str, Any], project_root: Path) -> None:
        self.cfg = cfg
        self.project_root = project_root
        self.reference_path = ReferencePath(cfg, project_root)
        self.ekf = TrajectoryEKF(cfg, self.reference_path)
        self.optimizer = SlidingWindowOptimizer(cfg, self.reference_path)
        self.measurements: list[MeasurementStep] = []
        self.ekf_states: list[np.ndarray] = []
        self.optimized_states: list[np.ndarray] = []
        self.odom_states: list[np.ndarray] = []
        self.prev_frame: Optional[TelemetryFrame] = None
        self.prev_raw_timestamp: Optional[int] = None
        self.timestamp_wrap_count = 0
        self.last_optimize_time_s = 0.0
        self.distance_per_count = math.pi * float(cfg_get(cfg, "robot", "wheel_diameter_m", default=0.065)) / float(
            cfg_get(cfg, "robot", "encoder_counts_per_rev", default=390.0)
        )
        self.track_width_m = float(cfg_get(cfg, "robot", "track_width_m", default=0.145))
        self.left_count_sign = float(cfg_get(cfg, "robot", "left_count_sign", default=1.0))
        self.right_count_sign = float(cfg_get(cfg, "robot", "right_count_sign", default=1.0))
        self.max_dt_s = float(cfg_get(cfg, "robot", "max_dt_s", default=1.0))
        initial_pose = cfg_get(cfg, "robot", "initial_pose", default=[0.0, 0.0, 0.0])
        self.odom_pose = np.array(
            [float(initial_pose[0]), float(initial_pose[1]), math.radians(float(initial_pose[2]))],
            dtype=float,
        )

    def _count_bits(self, bits: int) -> int:
        return int(bin(bits & 0xFF).count("1"))

    def _line_error_m(self, bits: int) -> float:
        positions = list(cfg_get(self.cfg, "line_sensor", "positions_m", default=[]))
        if len(positions) != 8:
            return 0.0
        active = [float(positions[i]) for i in range(8) if bits & (1 << i)]
        if not active:
            return 0.0
        return float(sum(active) / len(active))

    def _unwrap_timestamp(self, timestamp_us: int) -> int:
        if self.prev_raw_timestamp is not None and timestamp_us < self.prev_raw_timestamp:
            self.timestamp_wrap_count += 1
        self.prev_raw_timestamp = timestamp_us
        return timestamp_us + self.timestamp_wrap_count * UINT32_WRAP

    def _build_measurement(self, frame: TelemetryFrame) -> MeasurementStep:
        timestamp_unwrapped = self._unwrap_timestamp(frame.timestamp_us)
        timestamp_s = timestamp_unwrapped * 1e-6
        dt_s = 0.0
        delta_left_counts = 0
        delta_right_counts = 0
        if self.prev_frame is not None and self.measurements:
            dt_s = timestamp_s - self.measurements[-1].timestamp_s
            delta_left_counts = frame.encoder_left - self.prev_frame.encoder_left
            delta_right_counts = frame.encoder_right - self.prev_frame.encoder_right
        if dt_s < 0.0 or dt_s > self.max_dt_s:
            dt_s = 0.0
            delta_left_counts = 0
            delta_right_counts = 0
        signed_left_counts = int(round(delta_left_counts * self.left_count_sign))
        signed_right_counts = int(round(delta_right_counts * self.right_count_sign))
        distance_left_m = signed_left_counts * self.distance_per_count
        distance_right_m = signed_right_counts * self.distance_per_count
        linear_velocity_mps = 0.0
        angular_velocity_radps = 0.0
        if dt_s > 0.0:
            linear_velocity_mps = 0.5 * (distance_left_m + distance_right_m) / dt_s
            # Encoder channels are physically swapped on this robot:
            # el = physical right, er = physical left → use (dl-dr) for CCW-positive
            angular_velocity_radps = (distance_left_m - distance_right_m) / max(self.track_width_m * dt_s, 1e-6)
        # MCU yaw is CW-positive (positive=right); EKF uses CCW-positive → negate
        yaw_rad = -math.radians(frame.yaw_deg)
        # MCU gyro (g_fastYawRate) is CW-positive → negate for CCW-positive
        gyro_radps = -math.radians(frame.gyro_z_dps)
        line_count = self._count_bits(frame.line_bits)
        line_error_m = self._line_error_m(frame.line_bits)
        slip_threshold = float(cfg_get(self.cfg, "ekf", "slip_threshold_radps", default=0.9))
        slip_detected = dt_s > 0.0 and abs(angular_velocity_radps - gyro_radps) > slip_threshold
        return MeasurementStep(
            index=len(self.measurements),
            mode=frame.mode,
            flags=frame.flags,
            timestamp_us_raw=frame.timestamp_us,
            timestamp_us_unwrapped=timestamp_unwrapped,
            timestamp_s=timestamp_s,
            dt_s=dt_s,
            encoder_left=frame.encoder_left,
            encoder_right=frame.encoder_right,
            delta_left_counts=signed_left_counts,
            delta_right_counts=signed_right_counts,
            distance_left_m=distance_left_m,
            distance_right_m=distance_right_m,
            linear_velocity_mps=linear_velocity_mps,
            angular_velocity_radps=angular_velocity_radps,
            yaw_deg=frame.yaw_deg,
            yaw_rad=yaw_rad,
            gyro_z_dps=frame.gyro_z_dps,
            gyro_z_radps=gyro_radps,
            line_bits=frame.line_bits,
            line_count=line_count,
            line_error_m=line_error_m,
            pwm_left=frame.pwm_left,
            pwm_right=frame.pwm_right,
            slip_detected=slip_detected,
            imu_valid=frame.imu_valid,
            line_valid=frame.line_valid,
        )

    def _update_odom(self, measurement: MeasurementStep) -> None:
        if measurement.dt_s <= 0.0:
            self.odom_states.append(self.odom_pose.copy())
            return
        ds = 0.5 * (measurement.distance_left_m + measurement.distance_right_m)
        heading = measurement.yaw_rad
        self.odom_pose[0] += ds * math.cos(heading)
        self.odom_pose[1] += ds * math.sin(heading)
        self.odom_pose[2] = heading
        self.odom_states.append(self.odom_pose.copy())

    def _maybe_optimize(self) -> None:
        if not bool(cfg_get(self.cfg, "optimization", "enabled", default=True)):
            return
        if not self.measurements:
            return
        current_time_s = self.measurements[-1].timestamp_s
        interval_s = float(cfg_get(self.cfg, "optimization", "trigger_interval_s", default=0.5))
        if current_time_s - self.last_optimize_time_s < interval_s:
            return
        window_size = int(cfg_get(self.cfg, "optimization", "window_size", default=40))
        start = max(0, len(self.measurements) - window_size)
        measurements = self.measurements[start:]
        states = []
        for i in range(start, len(self.ekf_states)):
            if i < len(self.optimized_states):
                states.append(self.optimized_states[i].copy())
            else:
                states.append(self.ekf_states[i].copy())
        optimized = self.optimizer.optimize(measurements, states)
        for offset, state in enumerate(optimized):
            idx = start + offset
            if idx < len(self.optimized_states):
                self.optimized_states[idx] = state.copy()
            else:
                self.optimized_states.append(state.copy())
        if optimized:
            self.ekf.x = optimized[-1].copy()
            self.last_optimize_time_s = current_time_s

    def ingest(self, frame: TelemetryFrame) -> None:
        measurement = self._build_measurement(frame)
        self._update_odom(measurement)
        self.ekf.predict(measurement.dt_s)
        if measurement.dt_s > 0.0:
            self.ekf.update_encoder(measurement.linear_velocity_mps, measurement.angular_velocity_radps, measurement.slip_detected)
        if measurement.imu_valid:
            self.ekf.update_yaw(measurement.yaw_rad)
            self.ekf.update_gyro(measurement.gyro_z_radps)
        if self.reference_path.enabled and measurement.line_valid and measurement.line_count >= int(
            cfg_get(self.cfg, "line_sensor", "min_active_count", default=1)
        ):
            self.ekf.update_line(measurement.line_error_m)
        self.measurements.append(measurement)
        self.ekf_states.append(self.ekf.x.copy())
        self.optimized_states.append(self.ekf.x.copy())
        self.prev_frame = frame
        self._maybe_optimize()

    def path_arrays(self) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        odom = np.asarray([[state[0], state[1]] for state in self.odom_states], dtype=float) if self.odom_states else np.zeros((0, 2), dtype=float)
        ekf = np.asarray([[state[0], state[1]] for state in self.ekf_states], dtype=float) if self.ekf_states else np.zeros((0, 2), dtype=float)
        opt = np.asarray([[state[0], state[1]] for state in self.optimized_states], dtype=float) if self.optimized_states else np.zeros((0, 2), dtype=float)
        return odom, ekf, opt

    def build_dataframe(self) -> pd.DataFrame:
        rows: list[dict[str, Any]] = []
        for i, measurement in enumerate(self.measurements):
            ekf_state = self.ekf_states[i]
            opt_state = self.optimized_states[i]
            odom_state = self.odom_states[i] if i < len(self.odom_states) else self.odom_pose
            ref_error_m: Optional[float] = None
            ref_error_valid = False
            if self.reference_path.enabled:
                ref_error_m = self.reference_path.signed_distance(float(opt_state[0]), float(opt_state[1]))
                ref_error_valid = ref_error_m is not None
            rows.append(
                {
                    **asdict(measurement),
                    "is_running": bool(measurement.flags & 0x01),
                    "mode_name": mode_name_from_id(measurement.mode),
                    "odom_x_m": float(odom_state[0]),
                    "odom_y_m": float(odom_state[1]),
                    "odom_theta_deg": math.degrees(float(odom_state[2])),
                    "ekf_x_m": float(ekf_state[0]),
                    "ekf_y_m": float(ekf_state[1]),
                    "ekf_theta_deg": math.degrees(float(ekf_state[2])),
                    "ekf_v_mps": float(ekf_state[3]),
                    "ekf_omega_radps": float(ekf_state[4]),
                    "ekf_gyro_bias_radps": float(ekf_state[5]),
                    "opt_x_m": float(opt_state[0]),
                    "opt_y_m": float(opt_state[1]),
                    "opt_theta_deg": math.degrees(float(opt_state[2])),
                    "opt_v_mps": float(opt_state[3]),
                    "opt_omega_radps": float(opt_state[4]),
                    "opt_gyro_bias_radps": float(opt_state[5]),
                    "ref_error_m": float(ref_error_m) if ref_error_valid else np.nan,
                    "ref_error_valid": bool(ref_error_valid),
                }
            )
        return pd.DataFrame(rows)

    def build_summary(self, parser: BinaryFrameParser) -> dict[str, Any]:
        df = self.build_dataframe()
        if df.empty:
            return {
                "sample_count": 0,
                "parser": {
                    "frames_ok": parser.frames_ok,
                    "frames_bad_checksum": parser.frames_bad_checksum,
                    "frames_bad_tail": parser.frames_bad_tail,
                    "bytes_discarded": parser.bytes_discarded,
                },
            }
        opt_xy = df[["opt_x_m", "opt_y_m"]].to_numpy(dtype=float)
        deltas = np.diff(opt_xy, axis=0) if len(opt_xy) > 1 else np.zeros((0, 2), dtype=float)
        path_length_m = float(np.sum(np.linalg.norm(deltas, axis=1))) if len(deltas) > 0 else 0.0
        direct_distance_m = float(np.linalg.norm(opt_xy[-1] - opt_xy[0])) if len(opt_xy) > 0 else 0.0
        motion_summary = build_motion_summary(df, self.cfg)
        return {
            "sample_count": int(len(df)),
            "duration_s": float(df["timestamp_s"].iloc[-1] - df["timestamp_s"].iloc[0]) if len(df) > 1 else 0.0,
            "timestamp_wrap_count": int(self.timestamp_wrap_count),
            "path_length_m": path_length_m,
            "direct_distance_m": direct_distance_m,
            "mean_linear_velocity_mps": float(df["linear_velocity_mps"].abs().mean()),
            "max_linear_velocity_mps": float(df["linear_velocity_mps"].abs().max()),
            "mean_pwm_left": float(df["pwm_left"].mean()),
            "mean_pwm_right": float(df["pwm_right"].mean()),
            "slip_ratio": float(df["slip_detected"].mean()),
            "line_detect_ratio": float((df["line_count"] > 0).mean()),
            "final_opt_x_m": float(df["opt_x_m"].iloc[-1]),
            "final_opt_y_m": float(df["opt_y_m"].iloc[-1]),
            "final_opt_theta_deg": float(df["opt_theta_deg"].iloc[-1]),
            "trajectory_quality": motion_summary.get("trajectory_quality", {}),
            "motion_state": motion_summary.get("motion_state", {}),
            "reference_tracking": motion_summary.get("reference_tracking", {}),
            "parser": {
                "frames_ok": parser.frames_ok,
                "frames_bad_checksum": parser.frames_bad_checksum,
                "frames_bad_tail": parser.frames_bad_tail,
                "bytes_discarded": parser.bytes_discarded,
            },
        }


def load_config(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as fp:
        data = yaml.safe_load(fp)
    if not isinstance(data, dict):
        raise ValueError(f"Invalid config file: {path}")
    return data


def send_command(port: serial.Serial, command: str, timeout_s: float = 0.6) -> list[str]:
    payload = command if command.endswith("!") else f"{command}!"
    port.write(payload.encode("utf-8"))
    port.flush()
    deadline = time.time() + timeout_s
    received = []
    while time.time() < deadline:
        line = port.readline().decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        received.append(line)
        if "OK " in line or line.startswith("OK") or line == "ERR" or "ERR" in line:
            break
    return received


def ensure_command_ok(command: str, response: list[str]) -> None:
    if any(line == "ERR" or "ERR" in line for line in response):
        raise RuntimeError(f"Command failed: {command} -> {response}")
    if not any(line.startswith("OK") or "OK " in line for line in response):
        raise RuntimeError(f"Command did not return OK: {command} -> {response}")


def build_setup_commands(cfg: dict[str, Any]) -> list[str]:
    commands: list[str] = []
    protocol = str(cfg_get(cfg, "telemetry", "protocol", default="BIN")).upper()
    mode = normalize_mode_name(cfg_get(cfg, "serial", "mode", default=MODE_TRACK))
    mode_spec = MODE_COMMAND_SPECS[mode]
    mode_cfg = cfg_get(cfg, "commands", mode_spec["config_key"], default={}) or {}

    commands.append(f"#MODE={mode}!")

    if protocol == "TEXT":
        # New firmware: individual PID parameter commands
        speed_pid = mode_cfg.get("speed_pid")
        if isinstance(speed_pid, list) and len(speed_pid) == 3:
            commands.append(f"#SKP={speed_pid[0]}!")
            commands.append(f"#SKI={speed_pid[1]}!")
            commands.append(f"#SKD={speed_pid[2]}!")
        if mode == MODE_STRAIGHT:
            angle_pid = mode_cfg.get("angle_pid")
            if isinstance(angle_pid, list) and len(angle_pid) == 3:
                commands.append(f"#AKP={angle_pid[0]}!")
                commands.append(f"#AKI={angle_pid[1]}!")
                commands.append(f"#AKD={angle_pid[2]}!")
        elif mode == MODE_TRACK:
            line_pid = mode_cfg.get("line_pid")
            if isinstance(line_pid, list) and len(line_pid) == 3:
                commands.append(f"#LKP={line_pid[0]}!")
                commands.append(f"#LKI={line_pid[1]}!")
                commands.append(f"#LKD={line_pid[2]}!")
        commands.append(f"#SPD={float(mode_cfg.get('target_speed', mode_spec['default_target_speed']))}!")
    else:
        # Legacy binary firmware: PID=MODE,AXIS,kp,ki,kd format
        for pid_key, pid_axis in mode_spec["pid_entries"]:
            pid_values = mode_cfg.get(pid_key)
            if isinstance(pid_values, list) and len(pid_values) == 3:
                commands.append(f"#PID={mode},{pid_axis},{pid_values[0]},{pid_values[1]},{pid_values[2]}!")
        commands.append(f"#TS={float(mode_cfg.get('target_speed', mode_spec['default_target_speed']))}!")
        if mode == MODE_TRACK and "pwm_max" in mode_cfg:
            commands.append(f"#PWM_MAX={int(mode_cfg['pwm_max'])}!")
        commands.append(f"#PROTO={protocol}!")
    return commands


def apply_overrides(cfg: dict[str, Any], args: argparse.Namespace) -> None:
    if args.port:
        cfg.setdefault("serial", {})["port"] = args.port
    if args.mode:
        cfg.setdefault("serial", {})["mode"] = args.mode.upper()
    if args.duration is not None:
        cfg.setdefault("serial", {})["duration_s"] = float(args.duration)
    if args.no_plot:
        cfg.setdefault("output", {})["live_plot"] = False
    if args.output_dir:
        cfg.setdefault("output", {})["root_dir"] = args.output_dir
    if hasattr(args, 'aki') and args.aki is not None:
        cfg["_aki_override"] = float(args.aki)


def make_output_prefix(cfg: dict[str, Any], project_root: Path) -> tuple[Path, str]:
    root_dir = resolve_path(project_root, str(cfg_get(cfg, "output", "root_dir", default="000Data/trajectory_reconstruction")))
    root_dir.mkdir(parents=True, exist_ok=True)
    prefix = str(cfg_get(cfg, "output", "csv_name_prefix", default="trajectory"))
    stamp = time.strftime("%Y%m%d_%H%M%S")
    return root_dir, f"{prefix}_{stamp}"


def run_capture(cfg: dict[str, Any], config_path: Path) -> tuple[Path, Path, Optional[Path], Optional[Path], dict[str, Any]]:
    project_root = config_path.parent.parent
    output_dir, stem = make_output_prefix(cfg, project_root)
    csv_path = output_dir / f"{stem}.csv"
    json_path = output_dir / f"{stem}_summary.json"
    png_path = output_dir / f"{stem}.png"
    raw_bin_path = output_dir / f"{stem}.bin" if bool(cfg_get(cfg, "output", "save_raw_bin", default=True)) else None

    analyzer = TrajectoryAnalyzer(cfg, project_root)
    plotter = LivePlotter(bool(cfg_get(cfg, "output", "live_plot", default=True)))

    port_name = str(cfg_get(cfg, "serial", "port", default="COM18"))
    baudrate = int(cfg_get(cfg, "serial", "baudrate", default=115200))
    timeout_s = float(cfg_get(cfg, "serial", "timeout_s", default=0.05))
    read_chunk_size = int(cfg_get(cfg, "serial", "read_chunk_size", default=512))
    duration_s = float(cfg_get(cfg, "serial", "duration_s", default=20.0))
    start_after_config = bool(cfg_get(cfg, "serial", "start_after_config", default=True))
    start_command = str(cfg_get(cfg, "serial", "start_command", default="#RUN!"))
    stop_command = str(cfg_get(cfg, "serial", "stop_command", default="#STOP!"))

    protocol = str(cfg_get(cfg, "telemetry", "protocol", default="BIN")).upper()
    mode_id = 0 if normalize_mode_name(cfg_get(cfg, "serial", "mode", default=MODE_TRACK)) == MODE_STRAIGHT else 1
    if protocol == "TEXT":
        parser: Any = TextFrameParser(mode=mode_id)
    else:
        parser = BinaryFrameParser(
            head=list(cfg_get(cfg, "telemetry", "frame_head", default=[170, 85])),
            tail=list(cfg_get(cfg, "telemetry", "frame_tail", default=[13, 10])),
            frame_size=int(cfg_get(cfg, "telemetry", "frame_size", default=33)),
        )

    raw_fp = raw_bin_path.open("wb") if raw_bin_path is not None else None
    try:
        with serial.Serial(port_name, baudrate=baudrate, timeout=timeout_s) as port:
            port.reset_input_buffer()
            port.reset_output_buffer()
            for command in build_setup_commands(cfg):
                ensure_command_ok(command, send_command(port, command))
                time.sleep(0.05)
            # Inject AKI override if specified via --aki
            aki_val = cfg.get("_aki_override")
            if aki_val is not None:
                aki_cmd = f"#AKI={aki_val}!"
                print(f"[AKI OVERRIDE] Sending {aki_cmd}")
                ensure_command_ok(aki_cmd, send_command(port, aki_cmd))
                time.sleep(0.05)
            if start_after_config:
                ensure_command_ok(start_command, send_command(port, start_command))
            start_time = time.time()
            last_plot_time = 0.0
            while True:
                if duration_s > 0.0 and (time.time() - start_time) >= duration_s:
                    break
                chunk = port.read(read_chunk_size)
                if not chunk:
                    continue
                if raw_fp is not None:
                    raw_fp.write(chunk)
                frames = parser.push(chunk)
                for frame in frames:
                    analyzer.ingest(frame)
                if analyzer.measurements and (time.time() - last_plot_time) >= 0.05:
                    odom_xy, ekf_xy, opt_xy = analyzer.path_arrays()
                    plotter.update(odom_xy, ekf_xy, opt_xy)
                    last_plot_time = time.time()
            stop_response = send_command(port, stop_command)
            if any(line == "ERR" for line in stop_response):
                raise RuntimeError(f"Command failed: {stop_command} -> {stop_response}")
    finally:
        if raw_fp is not None:
            raw_fp.close()

    df = analyzer.build_dataframe()
    df.to_csv(csv_path, index=False, encoding="utf-8-sig")
    summary = analyzer.build_summary(parser)
    if bool(cfg_get(cfg, "output", "save_summary_json", default=True)):
        json_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    else:
        json_path = output_dir / f"{stem}_summary_disabled.json"
    if bool(cfg_get(cfg, "output", "save_plot_png", default=True)):
        plotter.save(png_path)
    else:
        png_path = None
    return csv_path, json_path, png_path, raw_bin_path, summary


def compute_drift_metrics(df: pd.DataFrame) -> dict[str, Any]:
    """Compute lateral drift metrics from the optimized trajectory."""
    run_df = df[df["is_running"] == True].copy()  # noqa: E712
    if run_df.empty or len(run_df) < 10:
        return {"error": "not enough running samples"}

    # Use odom trajectory (IMU yaw heading + encoder distance): most accurate for this robot.
    # EKF is not well-tuned and introduces more error than it removes.
    x = run_df["odom_x_m"].to_numpy(dtype=float)
    y = run_df["odom_y_m"].to_numpy(dtype=float)
    t = run_df["timestamp_s"].to_numpy(dtype=float)
    yaw = run_df["yaw_deg"].to_numpy(dtype=float)

    # Lateral offset = y-coordinate in the initial heading frame
    # Car starts at (0,0) facing +x in CCW-positive convention
    # y > 0 = LEFT of initial heading
    cross_mm = y * 1000.0

    final_lat_mm = float(cross_mm[-1])
    rms_mm = float(np.sqrt(np.mean(cross_mm**2)))
    max_abs_mm = float(np.max(np.abs(cross_mm)))
    yaw_mean = float(np.mean(yaw))
    yaw_std = float(np.std(yaw))
    duration_s = float(t[-1] - t[0])

    # Direction: y > 0 = LEFT in CCW-positive coordinate system
    # But cross > 0 depends on travel direction; use final_lat sign
    if abs(final_lat_mm) < 3.0 and abs(yaw_mean) < 0.2:
        direction = "straight"
    elif final_lat_mm > 0 or yaw_mean < -0.2:
        direction = "LEFT"
    else:
        direction = "RIGHT"

    return {
        "final_lateral_mm": round(final_lat_mm, 2),
        "rms_lateral_mm": round(rms_mm, 2),
        "max_abs_lateral_mm": round(max_abs_mm, 2),
        "yaw_mean_deg": round(yaw_mean, 4),
        "yaw_std_deg": round(yaw_std, 4),
        "direction": direction,
        "duration_s": round(duration_s, 2),
        "sample_count": int(len(run_df)),
    }


def replay_log_file(cfg: dict[str, Any], log_path: Path, config_path: Path) -> dict[str, Any]:
    """Replay a saved HB text log through the EKF pipeline."""
    import re
    project_root = config_path.parent.parent
    analyzer = TrajectoryAnalyzer(cfg, project_root)
    text_parser = TextFrameParser(mode=0)

    with log_path.open("r", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("HB:"):
                continue
            frames = text_parser.push((line + "\n").encode("utf-8"))
            for frame in frames:
                analyzer.ingest(frame)

    df = analyzer.build_dataframe()
    if df.empty:
        return {"error": "no frames parsed", "file": str(log_path)}

    drift = compute_drift_metrics(df)
    return {
        "file": str(log_path.name),
        "total_frames": len(df),
        "drift": drift,
    }


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(Path(__file__).with_name("config.yaml")))
    parser.add_argument("--port")
    parser.add_argument("--mode", choices=["STRAIGHT", "TRACK"])
    parser.add_argument("--duration", type=float)
    parser.add_argument("--output-dir")
    parser.add_argument("--no-plot", action="store_true")
    parser.add_argument("--aki", type=float, help="Override AKI (heading integral gain) for this run")
    parser.add_argument("--replay", type=str, help="Replay a saved HB log file (offline analysis)")
    return parser


def main() -> None:
    parser = build_arg_parser()
    args = parser.parse_args()
    config_path = Path(args.config).resolve()
    cfg = load_config(config_path)
    apply_overrides(cfg, args)

    # Offline replay mode
    if hasattr(args, 'replay') and args.replay:
        log_path = Path(args.replay).resolve()
        if not log_path.exists():
            print(f"ERROR: file not found: {log_path}")
            return
        result = replay_log_file(cfg, log_path, config_path)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return

    csv_path, json_path, png_path, raw_bin_path, summary = run_capture(cfg, config_path)
    print(f"CSV={csv_path}")
    print(f"SUMMARY={json_path}")
    if png_path is not None:
        print(f"PLOT={png_path}")
    if raw_bin_path is not None:
        print(f"RAW={raw_bin_path}")

    # Compute and display drift metrics
    df_path = csv_path
    try:
        df = pd.read_csv(df_path)
        drift = compute_drift_metrics(df)
        summary["drift"] = drift
        print("\n" + "="*60)
        print(f"  DRIFT: {drift.get('direction','?')} | "
              f"final={drift.get('final_lateral_mm',0):+.1f}mm | "
              f"RMS={drift.get('rms_lateral_mm',0):.1f}mm | "
              f"yaw_mean={drift.get('yaw_mean_deg',0):+.3f}\u00b0")
        print("="*60)
    except Exception as e:
        print(f"Drift analysis error: {e}")

    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
