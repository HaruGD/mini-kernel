# Windows GUI Domain And Compatibility Runtime

This document defines a feasible long-term architecture for using a
user-supplied Windows VM as both a Windows application runtime and an optional
accelerated presentation domain for OS64. It replaces the less precise idea of
"Windows as the GUI server" with an explicit trust, ownership, recovery, and
implementation model.

This is a post-native-desktop research goal. It does not change the current
Phase 4 implementation order, and it does not permit the native display,
input, or recovery paths to be removed.

## 1. Goal

The primary product motivations are:

1. run Windows applications that have no native OS64 build or practical native
   substitute; and
2. play legitimately accessed DRM-protected video through the original
   Windows application or browser, its licensed content-decryption system, and
   the Windows protected media and output stack.

The Windows presentation domain is the integration layer that makes those
capabilities feel like part of one desktop. It is not the reason to trust
Windows with Host policy or to remove the native desktop.

OS64 remains the system owner:

- boot and shutdown;
- CPU, memory, process, and thread scheduling;
- storage and filesystem ownership;
- physical input ownership and secure-attention handling;
- device and IOMMU policy;
- native application execution;
- VM creation, destruction, and resource limits;
- security decisions, diagnostics, and recovery;
- the authoritative state of OS64-native windows and surfaces.

The Windows VM provides two optional runtime roles:

```text
Windows Compatibility Runtime
├─ Windows Application Runtime
│  ├─ Win32 and Windows services
│  ├─ DirectX
│  ├─ licensed protected-media playback
│  └─ supported Windows kernel drivers
└─ Windows Presentation Domain
   ├─ proxy HWNDs for OS64-native windows
   ├─ DWM composition and Windows protected scanout
   └─ an exclusively assigned high-performance GPU
```

The concise description is:

> OS64 owns the machine and all security-relevant state. A Windows VM is an
> optional, replaceable, and untrusted compatibility and presentation domain.

Windows is not a required boot dependency. If it cannot start or must be
terminated, OS64 returns to a complete native recovery and administration
path.

## 2. Non-Goals

This project does not:

- reimplement the Windows API or Windows kernel ABI;
- distribute Windows images, product keys, Microsoft binaries, proprietary
  GPU drivers, games, or third-party kernel drivers;
- hide virtualization or bypass VM detection, anti-cheat, DRM enforcement,
  output protection, licensing, or online-service policy;
- extract, capture, map into Host memory, or redistribute decrypted protected
  media;
- promise that every Windows kernel driver or application supports a VM;
- promise that a content provider permits playback in the supported VM profile
  or that every title, resolution, codec, and DRM security level is available;
- trust Windows-rendered pixels for Host authentication or permission prompts;
- grant the Windows guest unrestricted access to Host storage or memory;
- require Windows for the native desktop, terminal, diagnostics, or recovery.

## 3. Trust And Authority Model

Using DWM as the normal-mode final compositor makes Windows highly important
to availability, but it must not make Windows authoritative for Host security.

### Host-Authoritative State

The following state always belongs to OS64:

- native `window_id` plus generation;
- native surface object and content generation;
- native application identity and permissions;
- accepted native geometry, visibility, and lifecycle state;
- physical input stream and secure-attention sequence;
- file-sharing capabilities;
- VM identity, guest-memory mappings, and device assignments;
- display-mode ownership and recovery generation;
- the decision to start, stop, reset, or quarantine the VM.

### Guest-Authoritative State

Windows owns only its internal domain state:

- Windows processes and services;
- Windows application windows;
- DWM internal surfaces and composition;
- Windows GPU command submission;
- Windows user-session state.

### Replicated State

The following objects are replicas and never become Host authority:

- proxy HWNDs representing native OS64 windows;
- guest D3D textures containing copied Host surface pixels;
- LunaShell lists of OS64-native applications;
- guest copies of title, icon, geometry, visibility, and focus state.

A compromised Guest Agent may lie about guest processes or pixels. Therefore,
the Host may enforce a file grant for the Windows VM as a whole, but cannot
claim a strong per-Windows-process security boundary based only on a Guest
Agent process name.

## 4. Secure UI Boundary

No security-sensitive Host UI may be rendered as a Windows proxy window.
Windows could imitate that window after compromise.

The following operations require native output and Host-owned input:

- permission and capability approval;
- login or administrator authentication;
- disk encryption and recovery-key entry;
- VM reset, termination, and device reassignment confirmation;
- secure file-sharing approval;
- panic, crash, and recovery controls;
- security warnings whose authenticity matters.

Entering secure UI revokes Guest input, changes the display mode to the native
path, draws through OS64's trusted presentation backend, and returns to the
Windows domain only after an explicit Host decision.

## 5. Host Architecture

The existing application ABI remains unchanged:

```text
native application
    -> Window SDK
    -> windowd: authoritative native window state
         ├─ Native Presentation Backend
         │    -> native composition
         │    -> displayd
         │    -> GOP, iGPU, or native GPU driver
         └─ Windows Presentation Backend
              -> winpresentd
              -> versioned virtual device and shared queues
              -> Windows bridge
```

The component names `winpresentd`, `display-supervisor`, and `vmd` are
architectural placeholders, not current binaries.

`windowd` continues to own native window policy regardless of active
presentation backend. In native mode it composites current surfaces itself. In
Windows mode it can either export the complete native composite or export
individual native windows. Switching presentation backends never changes an
application's Window SDK contract.

The native backend remains capable of displaying all native windows. It is not
reduced to a boot logo, because recovery must be able to show running native
applications and preserved surfaces after the Windows domain fails.

## 6. Output Topology

An exclusively assigned RTX GPU cannot simultaneously be a Host recovery
device. The physical display topology must be selected before GPU passthrough
is treated as complete.

### Profile A: Dual Monitor Inputs

```text
AMD iGPU  -> monitor input A: native and recovery
RTX guest -> monitor input B: Windows normal mode
```

This is the recommended first hardware profile and the first candidate for DRM
qualification because Windows owns a direct RTX-to-monitor link. It is simple
and preserves an independent recovery path, but changing monitor inputs may
require a physical button, DDC/CI support, or an external switch. Seamless
automatic switching is not guaranteed.

### Profile B: Host-Owned Scanout

```text
Windows renders on RTX
    -> copy or shared presentation path
    -> Host iGPU scanout
    -> monitor
```

This allows seamless native overlays and recovery, but requires an efficient
cross-GPU frame-transfer path and may add latency or bandwidth cost to every
Windows frame. It is not a baseline DRM profile: protected video cannot be
assumed to permit readback or copying through a Host-visible frame buffer.

### Profile C: Hardware Display Mux

A hardware mux can switch the monitor between Host and Guest GPUs under Host
control. This provides direct Guest scanout and deterministic recovery and may
support protected playback when the complete Guest GPU-to-monitor link meets
the DRM policy. It depends on specific hardware and must not be assumed on
ordinary desktop systems.

OS64 must publish which profile a machine uses. A failure to switch output is a
recoverable platform limitation, not a reason to leave input owned by the
guest.

## 7. Display And Recovery State

The future Windows domain extends, rather than replaces, the Phase 4 console
and GUI handoff:

```text
NATIVE_RECOVERY
    -> VM_BOOTING
    -> BRIDGE_NEGOTIATING
    -> WINDOWS_PRESENTING
    -> RECOVERING
    -> NATIVE_RECOVERY or WINDOWS_PRESENTING
```

Every transition carries a nonzero Host display-session generation and exact
VM and Guest Agent identities. Old proxy, present, input, and release messages
are rejected after any VM or bridge restart.

`display-supervisor` is responsible for policy and observes:

- VM and vCPU liveness;
- Guest Agent heartbeat and session generation;
- proxy process and LunaShell health;
- DWM/frame-production progress;
- accepted buffer and present-fence progress;
- passed-through GPU and output state;
- Host-to-Guest queue progress;
- physical input ownership.

Failure recovery proceeds in this order:

1. revoke Guest physical input;
2. stop accepting presents for the failed session generation;
3. activate the native output profile;
4. reconstruct the native composite from Host-owned windows and surfaces;
5. present native recovery controls;
6. release or reset failed bridge and passthrough resources safely;
7. restart or quarantine Windows according to bounded policy;
8. rebuild proxy objects from current Host state only after a new handshake;
9. return to Windows output after a complete acknowledged frame.

No Guest heartbeat is sufficient by itself. A responsive agent with a hung
DWM or GPU is still a presentation failure.

## 8. Window Ownership And Cross-Domain Policy

Native and Windows windows have different authorities:

- a native window is authoritative in `windowd` and replicated into one proxy
  HWND when per-window integration is enabled;
- a Windows window is authoritative inside the Windows VM;
- DWM owns final visual ordering only while `WINDOWS_PRESENTING` is active;
- Host security does not depend on DWM ordering or appearance;
- LunaShell requests native-window state changes but `windowd` validates and
  commits them;
- a Host rejection causes the proxy to return to the last accepted state.

Native proxy identity contains at least:

```text
host_boot_generation
vm_session_generation
bridge_generation
window_id
window_generation
surface_content_generation
```

This prevents PID, VM, window-slot, and proxy reuse from reviving stale state.

## 9. Presentation Bridge Protocol

The bridge is a bounded versioned protocol, preferably exposed through a
virtual PCI device with shared-memory queues and interrupt or event signaling.
The exact transport may change without changing the logical protocol.

Required control operations include:

```text
HELLO / CAPABILITIES
SESSION_BEGIN / SESSION_END
CREATE_PROXY / DESTROY_PROXY
SET_METADATA
SET_GEOMETRY / SET_VISIBILITY
IMPORT_BUFFER / RELEASE_BUFFER
DAMAGE
PRESENT
PRESENT_ACK / FENCE
FOCUS_REQUEST / FOCUS_RESULT
INPUT_EVENT
HEARTBEAT
RESET
```

Protocol requirements:

- fixed maximum queue depth, windows, buffers, rectangles, and message size;
- ABI size, version, flags, length, arithmetic, and enum validation;
- no Host or Guest raw pointer in a shared message;
- monotonic request, content, frame, and session generations;
- producer/consumer indexes updated with documented memory ordering;
- no overwrite of an unconsumed ring slot;
- explicit buffer ownership and release fence;
- bounded timeout and idempotent reset;
- full-frame recovery after a dropped or stale partial-damage sequence;
- exact format, color-space, stride, DPI, scale, and cursor semantics;
- malformed Guest messages affect only the Guest domain and cannot corrupt Host
  state.

The first protocol implementation should be tested with a Host-side simulator
and a Windows bridge process before it is attached to the real hypervisor.

## 10. Surface Transfer And Performance

Because the Host iGPU/GOP path and Guest RTX use different devices and address
spaces, complete zero-copy is not a baseline requirement.

### Stage 1: Full Native Desktop Surface

```text
windowd native composite
    -> one shared CPU-visible buffer
    -> one full-screen Windows proxy
    -> DWM
```

This validates transport, damage, synchronization, failure, and input without
cross-domain per-window complexity.

### Stage 2: Per-Window Buffers

```text
one Host native window surface
    -> one bridge buffer stream
    -> one Windows proxy HWND
```

Use double or triple buffering, bounded damage regions, and present fences.
The Guest bridge uploads changed pixels into a D3D texture. Native UI is
expected to be light enough for an initial CPU copy; optimization follows
measurement.

Windows games and graphics-heavy Windows applications render directly inside
the Guest on the assigned RTX and do not use the Host-surface copy path.

## 11. Protected Media And DRM Playback

Protected-media playback is a supported compatibility goal, not a DRM bypass.
The viable high-quality path keeps protected content inside the Windows trust
and output boundary:

```text
licensed Windows application or browser
    -> service-approved DRM/CDM and applicable Windows Protected Media Path
    -> signed Windows media and graphics components
    -> Guest-visible hardware trust plus RTX protected decode/render/scanout
    -> HDCP-capable HDMI or DisplayPort link
    -> monitor
```

The Host presentation bridge carries ordinary OS64-native pixels into Windows.
It never carries decrypted protected video pixels back into OS64, exposes them
as a shared surface, implements capture, or depends on Guest readback. Guest
control messages may report playback state and errors but contain no protected
media payload.

Protected playback is qualified per exact runtime profile. A profile records:

- Windows edition and build;
- application or browser and content-decryption component version;
- GPU, firmware, and signed Windows driver version;
- virtual firmware, TPM, Secure Boot, Guest-visible trusted-execution support,
  and assigned-device configuration;
- monitor, cable, connector, resolution, refresh rate, and negotiated HDCP
  capability;
- streaming service, test title, codec, DRM system, achieved resolution, and
  observed failure code;
- whether the provider permits that VM environment under its current policy.

Windows PlayReady hardware DRM protects keys and decrypted samples in hardware,
and some output policies require HDCP. Meeting the hardware path is necessary
for those profiles but does not force a provider to issue a license to a VM.
Software DRM or a lower resolution may work in another profile, but it is not
used as evidence that high-resolution hardware DRM is complete.

Activating native secure UI may switch away from the Guest output and interrupt
or pause protected playback. That is an acceptable security consequence. OS64
does not weaken secure attention or keep an untrusted Guest output visible just
to preserve a video session.

## 12. Input Routing

All physical input first enters OS64:

```text
physical input
    -> Host input driver
    -> secure-attention detector
    -> Host input router
         ├─ native/recovery target
         └─ virtual HID injection into Windows
```

In Windows normal mode, DWM performs final hit testing. Input over a Windows
application remains inside the Guest. Input delivered to a native proxy HWND
is returned by the bridge to the exact Host window identity, where `windowd`
validates focus, session, sequence, timestamp, and coordinates before delivery
to the native application.

This first implementation has an additional Guest-to-Host round trip for
native windows. A lower-latency Host-side hit-test mirror may be added later,
but it must not disagree with the visible DWM ordering.

Secure attention is intercepted before virtual HID injection. It revokes Guest
input even if the VM, Guest Agent, DWM, or RTX is unresponsive. The same input
event is never delivered to both native and Guest paths.

## 13. VM And Hypervisor Prerequisites

Windows GUI integration starts only after OS64 has stable native execution,
threading, SMP, storage, networking, IOMMU, and device lifecycle.

The initial hypervisor platform requires:

- architecture-neutral VM, vCPU, guest-memory, interrupt, and device objects;
- Intel VMX and AMD SVM backends with EPT/NPT;
- bounded VM-exit dispatch and vCPU scheduling;
- virtual APIC, timers, ACPI, PCI, storage, networking, keyboard, pointer, and
  initial display devices;
- Guest UEFI boot and a virtual TPM/Secure Boot path for supported Windows 11
  configurations;
- an explicitly qualified Guest-visible hardware trust path when the selected
  DRM profile requires hardware-protected keys and media samples;
- deterministic guest reset and power state;
- isolated guest memory and explicit DMA mappings;
- a supervised `vmd` for lifecycle policy while latency-critical VM-exit,
  interrupt, and IOMMU mechanisms remain in the kernel.

Windows must first boot and remain stable on virtual devices. GPU passthrough
is not part of the first Windows boot milestone.

## 14. GPU Passthrough

Exclusive RTX assignment is a separate hardware milestone. Required work
includes:

- IOMMU DMA and interrupt remapping;
- PCIe isolation/ACS validation where available;
- exclusive Host/Guest ownership transitions;
- BAR and large 64-bit MMIO aperture allocation, including resizable BAR where
  applicable;
- MSI/MSI-X routing;
- device reset and failure containment;
- firmware and power-state handling;
- preservation of the Windows protected decode, protected scanout, and output
  protection path without Host mapping or readback;
- rejection when the GPU shares an unsafe isolation group;
- recovery when the device cannot be reset without a platform reboot.

The baseline never assigns the same GPU to Host and Guest simultaneously.
Consumer hardware support must be proven on an explicit hardware matrix rather
than inferred from model name alone.

Live VM save/restore is not assumed while a physical GPU is attached. Storage
snapshots are taken after the Guest is quiesced or stopped, and device state is
recreated through a new assignment session.

## 15. File, Clipboard, And Data Portals

Windows never receives the Host root filesystem or raw Host block device.

The first file-transfer implementation is a copy portal:

1. the user selects a Host file or directory in native trusted UI;
2. OS64 creates an expiring capability for the Windows VM;
3. a broker copies approved data into a bounded exchange area;
4. the Guest imports or exports through the bridge;
5. OS64 validates destination, size, name, and final commit;
6. the capability expires or is revoked.

Live shared folders and a virtual drive are later features. They require
defined behavior for path canonicalization, case sensitivity, symlinks,
locking, rename, deletion, cache coherence, quotas, and partial failure.

Capability fields include:

```text
capability_id + generation
VM session identity
Host object identity, not an unchecked path string
read/write direction
size and object-count limits
expiry and revocation state
```

Per-Windows-application restrictions may improve UX but are not a Host security
boundary if the Guest is compromised. Clipboard integration follows the same
rule and remains disabled in secure native mode.

## 16. Windows Guest Components

The Windows side is split into small replaceable components:

```text
bridge device driver
    -> virtual PCI/shared-memory transport and event signaling

Luna Guest Agent service
    -> handshake, heartbeat, proxy lifecycle, file/clipboard brokers

Proxy Window Host
    -> HWND and D3D texture presentation for native windows

LunaShell
    -> optional desktop/taskbar/launcher integration and status UI
```

The first supported configuration keeps Explorer and starts LunaShell as a
normal companion application. Full shell replacement is optional because
official Windows Shell Launcher support depends on Windows edition and has
deployment restrictions. OS64 must publish the exact supported editions and
runtime profile instead of assuming every user ISO can replace Explorer.

Guest kernel drivers require an appropriate Windows signing and installation
strategy. Development test signing is not a production distribution plan.

## 17. Installation And Distribution

Official OS64 releases contain no Windows image or proprietary third-party
driver:

```text
OS64 release
├─ native OS and recovery GUI
├─ hypervisor and VM manager
├─ open OS64 virtual-device definitions
├─ Windows Guest Agent and LunaShell produced by this project
├─ Windows Runtime Builder
└─ installation and compatibility documentation
```

The user supplies a valid Windows ISO, license, activation credentials, and
vendor GPU driver under their applicable terms.

The first installer uses two virtual optical devices:

```text
CD 1: untouched user-supplied Windows ISO
CD 2: OS64 Tools ISO
      ├─ autounattend.xml
      ├─ signed virtual-device drivers
      ├─ Guest Agent installer
      ├─ Proxy Window Host
      ├─ optional LunaShell installer
      └─ first-boot setup
```

This is preferred over modifying `boot.wim` and `install.wim` in the first
implementation. Image servicing remains an optional builder feature after the
ordinary Tools ISO path is reliable.

## 18. Runtime Images, Updates, And Snapshots

The virtual disk interface supports sparse allocation and copy-on-write, but
the on-disk format is selected separately. The architecture does not require
qcow2 unless OS64 deliberately implements and tests it.

Logical layout:

```text
windows-system-base.img       read-only validated base
windows-system-overlay.img    system and update changes
windows-data.img              applications and user data
```

Runtime compatibility metadata records:

```json
{
  "windows_build": "supported build range",
  "bridge_abi": 1,
  "agent_version": "...",
  "shell_version": "...",
  "virtual_hardware_profile": "...",
  "gpu_id": "...",
  "gpu_driver": "..."
}
```

Feature updates are staged against a snapshot and must pass VM boot, bridge,
input, output, recovery, and Windows-application tests before promotion.
Security updates must not be disabled indefinitely merely to avoid bridge
maintenance. An incompatible runtime enters native recovery with a clear
diagnostic rather than booting an unverified presentation domain.

Snapshots are storage and configuration checkpoints unless the active device
profile explicitly supports live device-state capture. With an assigned GPU,
the baseline uses Guest quiesce or shutdown, offline snapshot, restart, and
bridge regeneration.

## 19. Development Sequence

### Stage 0: Native Prerequisites

- close Phase 4 without console overwrite or `drive`;
- complete the native desktop and recovery path;
- complete Phase 4.5 threading and Phase 4.6 SMP;
- provide stable storage, networking, DMA/IOMMU, and device lifecycle.

Exit gate: OS64 remains administrable and can run native GUI applications
without Windows.

### Stage 1: Bridge Prototype Without A VM

- freeze the bridge ABI and limits;
- implement a Host simulator and Windows Proxy Window Host;
- transport one generated full-screen surface over ordinary development IPC;
- validate damage, fences, resizing, input return, restart, and malformed data.

Exit gate: bridge semantics are correct before hypervisor failures are added.

### Stage 2: Minimal Hypervisor

- bring up VMX/SVM, EPT/NPT, one vCPU, virtual interrupts, and guest reset;
- boot a small test guest;
- add multiple vCPUs only after the single-vCPU lifecycle is deterministic.

Exit gate: repeated start, stop, fault, reset, and memory pressure leave zero
unexplained Host resource drift.

### Stage 3: Windows On Virtual Devices

- provide Guest UEFI, storage, networking, input, and a simple virtual display;
- install a user-supplied Windows image;
- reach a stable Windows desktop inside one native OS64 window;
- add vTPM/Secure Boot support for the chosen Windows profile.

Exit gate: Windows boots and updates on virtual hardware without passthrough.

### Stage 4: Guest Control Plane

- install the bridge driver and Guest Agent;
- negotiate version/capabilities and heartbeat;
- supervise restart and stale-session cleanup;
- keep Explorer and run LunaShell only as a companion.

Exit gate: Guest Agent or VM restart cannot mutate current Host state with an
old session generation.

### Stage 5: Full-Screen Presentation Bridge

- export the complete native composite to one Windows proxy;
- return proxy input to Host routing;
- test frame pacing, damage, fences, DPI, cursor, and recovery;
- retain the virtual display and native output as fallback.

Exit gate: repeated bridge failure returns to native output without losing
native applications or surfaces.

### Stage 6: IOMMU And RTX Passthrough

- validate the explicit hardware profile;
- assign RTX exclusively to Windows;
- install the user-provided compatible driver;
- prove reset, interrupt, MMIO, DMA isolation, and native-output recovery;
- select and certify one physical output topology;
- validate the Windows protected media path, protected GPU output, HDCP
  negotiation, and representative service playback without Host pixel access.

Exit gate: Guest GPU load cannot DMA outside mapped Guest memory or prevent
Host input recovery and native diagnostics. The certified DRM test profile
either reaches its expected protected resolution or reports a documented
provider, license, driver, hardware, or output-policy incompatibility without
attempting circumvention.

### Stage 7: Per-Window Integration

- export each native window as an independent buffer stream;
- create one generation-tagged proxy HWND per native window;
- synchronize accepted move, resize, visibility, focus, and lifecycle state;
- integrate Windows and native entries in LunaShell UI;
- add clipboard and copy portal after the window path is stable.

Exit gate: proxy and Guest restarts recreate visual replicas from Host state
without killing native applications.

### Stage 8: Productization

- optional supported-edition shell replacement;
- Windows Runtime Builder and Tools ISO automation;
- compatibility manifests, offline snapshots, update qualification, and
  rollback;
- fault injection, resource accounting, long-duration soak, and hardware
  compatibility matrix;
- publish a separate protected-media matrix by Windows build, player, service,
  DRM level, GPU driver, display link, and achieved resolution.

Exit gate: installation and recovery are reproducible without distributing
Windows or hiding virtualization.

## 20. Current OS64 Mapping

Existing OS64 work already supplies the beginning of this architecture:

| Future responsibility | Current foundation |
| --- | --- |
| native surface ownership | page-backed surface objects and attenuated handles |
| native window authority | `windowd` owner/window/content generations |
| application abstraction | public Window SDK and mapped canvas |
| native presentation | `windowd -> displayd -> GOP` |
| input authority | Host input queue, `inputd`, and `windowd` focus routing |
| display recovery generation | Phase 4H console/GUI handoff contract |
| background execution | Phase 4H drive-free scheduler contract |
| thread and CPU foundation | planned Phase 4.5 and Phase 4.6 |
| Windows VM | post-roadmap hypervisor goal |

The current decision to combine window management and native composition in
`windowd` does not block the plan. The presentation backend can be split after
the native contract is stable and measured.

## 21. Final Invariants

The Windows GUI Domain is acceptable only while all of the following remain
true:

- OS64 boots, diagnoses, updates, and recovers without Windows;
- no Windows-rendered pixel is trusted as secure Host UI;
- physical input and secure attention always terminate at the Host first;
- native applications use one Window SDK regardless of presentation backend;
- Host native window and surface state survives Guest restart;
- a Guest owns no Host pointer, unrestricted DMA range, Host root filesystem,
  or stale session authority;
- GPU ownership is exclusive and IOMMU constrained;
- bridge queues and resource use are bounded and generation checked;
- unsupported Windows versions, GPUs, or drivers fail into native recovery;
- protected media remains inside the Windows protected path and direct Guest
  output; OS64 neither reads back decrypted frames nor claims universal DRM
  service compatibility;
- Windows and third-party proprietary components are supplied and licensed by
  the user;
- virtualization is disclosed and no compatibility feature attempts detection
  circumvention.

## Official Platform References

- [Microsoft: Desktop Window Manager overview](https://learn.microsoft.com/en-us/windows/win32/dwm/dwm-overview)
- [Microsoft: Shell Launcher overview and edition requirements](https://learn.microsoft.com/en-us/windows/configuration/shell-launcher/)
- [Microsoft: Discrete Device Assignment planning and limitations](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/plan/plan-for-deploying-devices-using-discrete-device-assignment)
- [Microsoft: Windows 11 and VM requirements](https://learn.microsoft.com/en-us/windows/whats-new/windows-11-requirements)
- [Microsoft: adding drivers during Windows Setup](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/add-device-drivers-to-windows-during-windows-setup?view=windows-11)
- [Microsoft: Protected Media Path](https://learn.microsoft.com/en-us/windows/win32/medfound/protected-media-path)
- [Microsoft: PlayReady DRM and output protection](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/playready-client-sdk)
