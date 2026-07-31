#!/usr/bin/env python3
import driver_alloc_test


if __name__ == "__main__":
    result = driver_alloc_test.main()
    if result == 0:
        print("driver sleepable, atomic, IRQ, emergency context test OK")
    raise SystemExit(result)
