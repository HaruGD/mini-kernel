# OS64 Roadmap

상태 표기:
- [x] 완료
- [~] 기반 구현 또는 안정화 진행 중
- [ ] 미구현

## 현재 기반

- [x] UEFI 부팅과 BootInfo/메모리 맵 전달
- [x] PMM, paging, NX, kernel heap, process user heap
- [x] FAT32 기반 root VFS와 memfs
- [x] ELF64 user program, scheduler, syscall
- [x] User SDK v2
  - 오류 코드
  - 시간 API
  - GOP 그래픽 API
  - 키보드 이벤트 API
- [x] Driver Manager v2
  - dependency
  - probe/bind
  - import/export ABI
  - IRQ ABI
  - unload/reload
  - signed DRV package
- [x] PCI discovery
- [x] GOP framebuffer driver
- [x] 2D graphics library and GOP present pipeline
  - surface/clipping/drawing primitives
  - bitmap blit, color-key blit, bitmap font/text
  - back buffer, dirty rectangles, partial present
  - 1280x800 and 800x600 screen smoke coverage
- [~] Common input-event path
  - stable key/pointer/common event ABI
  - bounded kernel event ring
  - drop-oldest overflow policy
  - PS/2 keyboard events produced into the common queue
  - nonblocking common event read syscall
  - blocking common event read syscall
  - PS/2 key poll/wait syscall remains compatible
  - input diagnostics are next
- [x] ACPI S5 shutdown

## 1단계: 커널 진단과 하드웨어 기반

GUI와 실컴 드라이버를 늘리기 전에 장애 원인을 안정적으로 추적할 수 있게 한다.

- [x] Panic subsystem
  - [x] exception register dump
  - [x] frame-pointer stack trace 기초
  - [x] panic 화면과 serial 동시 출력
  - [x] double-fault IST stack
- [x] Kernel log ring buffer
  - [x] log level과 subsystem tag
  - [x] 부팅 이후 로그 조회
  - [x] panic 시 최근 4 KiB 로그 출력
- [x] Diagnostic boot mode
  - [x] 외부 드라이버 autoload 없는 부팅
  - [x] 상세 UEFI/메모리/PCI 로그
  - [x] 전용 QEMU smoke test
- [x] ACPI table parser
  - [x] RSDP, RSDT/XSDT, MADT
  - [x] checksum과 table length 검증
- [x] Local APIC와 IOAPIC
  - [x] IRQ0/IRQ1 routing과 MADT override
  - [x] controller 공통 mask/EOI API
  - [x] ACPI/APIC 실패 시 PIC fallback
  - [x] PIC spurious IRQ7/IRQ15 ISR 판별과 EOI 처리
- [x] Phase 1 fault 회귀 자동화
  - [x] user/kernel page fault 분기
  - [x] user/kernel general protection fault 분기
  - [x] runtime ACPI 손상과 PIC fallback

## 2단계: 그래픽과 입력 기반

QEMU GOP 환경에서 먼저 완성하며 실제 GPU 드라이버는 요구하지 않는다.
세부 구현과 검증 순서는 `docs/phase2_task_breakdown.md`를 따른다.
2D 그래픽 라이브러리의 계층과 API 방향은 `docs/2d_graphics_library.md`를 따른다.

- [x] 2D graphics library
  - [x] pixel, line, rectangle
  - [x] bitmap/image blit
  - [x] bitmap font와 text
  - [x] clipping
  - [x] dirty rectangle
- [x] Double buffering
  - [x] back buffer
  - [x] framebuffer present
  - [x] 부분 화면 갱신
- [x] Input event queue
  - [x] common key/pointer/input event ABI
  - [x] bounded kernel event ring
  - [x] keyboard event queue
  - [x] mouse event 형식 예약
  - [x] overflow 정책
  - [x] blocking/nonblocking read
  - [x] shell diagnostics
- [~] Process event delivery
  - [x] 프로세스별 event queue
  - [x] focus 대상 선택
  - focus 대상 전달
  - 기존 PS/2와 이후 USB HID의 공통 이벤트 형식

## 3단계: IPC와 사용자 공간 서비스

창 서버와 서비스 매니저가 커널 내부 기능에 직접 결합되지 않도록 한다.

- [ ] IPC core
  - message channel
  - blocking send/receive
  - handle과 권한 검사
  - process 종료 시 정리
- [ ] Service Manager v1
  - 서비스 등록
  - 시작, 중지, 재시작
  - dependency와 상태 관리
  - 로그 연결
- [ ] 기본 사용자 공간 서비스
  - input service
  - display service
  - 향후 device service 기반

## 4단계: Compositor와 창 서버

- [ ] Framebuffer compositor prototype
  - surface 생성과 제거
  - z-order
  - damage tracking
  - 화면 합성
- [ ] Window server
  - 창 생성, 이동, 크기 변경
  - focus 관리
  - 키보드와 마우스 전달
  - 애플리케이션 IPC protocol
- [ ] GUI application SDK
  - window API
  - drawing surface API
  - input callback/event loop
  - 공통 widget 기초

## 5단계: GUI Desktop

- [ ] GUI terminal
- [ ] Desktop background
- [ ] Panel과 application launcher
- [ ] File manager
- [ ] 설정과 종료 UI

## 실컴 하드웨어 확장 트랙

이 트랙은 GUI 개발과 병행할 수 있지만 ACPI/APIC와 DMA 기반이 먼저 필요하다.

- [ ] DMA allocation/mapping API
- [ ] USB xHCI host controller
- [ ] USB enumeration과 hub
- [ ] USB HID keyboard와 mouse
- [ ] AHCI 또는 NVMe boot/storage driver
- [ ] Network driver와 network stack
- [ ] Audio subsystem과 driver
- [ ] 실제 GPU driver

## 가까운 작업 순서

1. Input event queue (`I02`~`I08`)
2. Process event delivery (`P01`~`P05`)
3. Phase 2 regression closure (`T01`~`T04`)
4. IPC core
5. Framebuffer compositor prototype
6. Window server prototype
7. GUI application SDK
8. Desktop shell prototype

## 원칙

- 커널에는 정책보다 최소 메커니즘을 둔다.
- 하드웨어 드라이버는 Driver Manager ABI를 사용한다.
- GUI 구성 요소와 서비스는 가능한 한 사용자 공간에서 실행한다.
- QEMU에서 자동 검증한 뒤 VirtualBox와 실컴으로 확장한다.
- 기능과 책임이 달라지면 파일을 분리하고, 줄 수만을 기준으로 억지로 나누지 않는다.
- 큰 단계는 한 가지 책임과 한 가지 검증 목표를 가진 작은 작업표로 나눈다.
