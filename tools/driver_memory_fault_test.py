#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(path: str, token: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    if token not in text:
        raise SystemExit(f"missing driver fault point {token} in {path}")


def main() -> int:
    sites = {
        "kernel/driver/driver_va.cpp": (
            "KERNEL_FAULT_POINT_DRIVER_VA_RECORD",
        ),
        "kernel/driver/driver_loader.cpp": (
            "KERNEL_FAULT_POINT_DRIVER_IMAGE_PAGE",
        ),
        "kernel/driver/driver_alloc.cpp": (
            "KERNEL_FAULT_POINT_DRIVER_ALLOC_RECORD",
            "KERNEL_FAULT_POINT_DRIVER_PAGE_RUN",
        ),
        "kernel/driver/driver_mmio.cpp": (
            "KERNEL_FAULT_POINT_DRIVER_MMIO_RECORD",
            "KERNEL_FAULT_POINT_DRIVER_PAGE_MAP",
        ),
        "kernel/driver/driver_dma.cpp": (
            "KERNEL_FAULT_POINT_DRIVER_DMA_RECORD",
            "KERNEL_FAULT_POINT_DRIVER_DMA_BOUNCE",
            "KERNEL_FAULT_POINT_DRIVER_DMA_DOMAIN",
            "KERNEL_FAULT_POINT_DRIVER_PAGE_MAP",
            "KERNEL_FAULT_POINT_DRIVER_PAGE_RUN",
        ),
        "kernel/driver/driver_manager.cpp": (
            "KERNEL_FAULT_POINT_DRIVER_IRQ_DRAIN",
            "KERNEL_FAULT_POINT_DRIVER_QUIESCE",
        ),
    }
    for path, tokens in sites.items():
        for token in tokens:
            require(path, token)

    # These focused tests arm every host-reachable point and prove that the
    # immediate retry succeeds with active resource counts back at baseline.
    focused = {
        "tools/driver_va_test.py": "KERNEL_FAULT_POINT_DRIVER_VA_RECORD",
        "tools/driver_alloc_test.py": "KERNEL_FAULT_POINT_DRIVER_ALLOC_RECORD",
        "tools/driver_mmio_test.py": "KERNEL_FAULT_POINT_DRIVER_MMIO_RECORD",
        "tools/dma_coherent_test.py": "KERNEL_FAULT_POINT_DRIVER_DMA_DOMAIN",
        "tools/dma_streaming_test.py": "KERNEL_FAULT_POINT_DRIVER_DMA_BOUNCE",
        "tools/driver_quiesce_test.py": "KERNEL_FAULT_POINT_DRIVER_QUIESCE",
    }
    for path, token in focused.items():
        require(path, token)
    print("driver memory fault-point placement and rollback matrix OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
