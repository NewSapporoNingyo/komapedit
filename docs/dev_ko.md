# 개발자 가이드

[README](README_ko.md) · [개발 진행 상황](../TODO.md) · [AI 코딩 도구 활용](ai-dev_ko.md)

이 가이드는 komapedit의 개발 환경, 아키텍처, 소스의 역할, 검증 절차를 다룹니다. AI 코딩 도구를 사용하는 작업에는 [`ai-dev_ko.md`](ai-dev_ko.md), [`AGENTS.md`](../AGENTS.md), [`.agents/skills`](../.agents/skills)의 해당 작업 절차도 적용합니다.

## 범위와 지원 환경

komapedit는 BVE Trainsim 맵을 보고 편집하는 C++17 Windows 데스크톱 애플리케이션입니다. Win32, DirectX 11, WIC, Dear ImGui, ImPlot을 사용하고 CMake와 Ninja로 빌드합니다.

런타임 구성 요소는 세 가지입니다.

- `maploader.dll`은 맵과 목록을 파싱하고 Include와 인코딩을 처리합니다. 기준 궤도와 다른 궤도의 선형을 생성하고 버전이 있는 타입 맵·편집 스냅샷을 관리합니다.
- `model_loader.dll`은 Assimp로 구조물 메시, 재질, 텍스처를 읽고 C ABI로 데이터를 제공합니다.
- `komapedit.exe`는 Win32/DirectX 11 GUI, 표, 2D 차트, 3D 미리 보기와 편집을 제공합니다.

구현 동작은 소스와 사용자 매뉴얼을 기준으로 합니다. 미완료 작업은 [`TODO.md`](../TODO.md), 완료 작업은 [`TODO_done.md`](TODO_done.md)에 기록합니다.

## 사전 준비

- Windows
- CMake 3.20 이상. 일반적인 Assimp 런타임 DLL 복사 경로를 사용하려면 3.21 이상을 권장합니다.
- Ninja
- MSVC 또는 MinGW 등 C++17 컴파일러
- Windows SDK, DirectX 11, WIC 개발 라이브러리
- Git
- CMake가 `assimp::assimp`로 찾을 수 있는 Assimp

Dear ImGui와 ImPlot을 가져옵니다.

```bat
.\get_3rd_party_packages.bat
```

Assimp는 별도로 설치합니다. vcpkg를 사용하는 경우 `VCPKG_ROOT`를 설정하고 도구 모음에 맞는 트리플릿을 설치합니다.

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
"%VCPKG_ROOT%\vcpkg.exe" install assimp:x64-mingw-dynamic
```

빌드 스크립트는 `VCPKG_ROOT`가 설정되어 있으면 vcpkg를 사용합니다. 기본 트리플릿은 `x64-mingw-dynamic`입니다. MSVC에서는 `VCPKG_DEFAULT_TRIPLET`에 `x64-windows` 등 맞는 트리플릿을 지정합니다. MinGW용 `install_Assimp.bat`는 `VCPKG_ROOT`를 사용하여 `assimp:x64-mingw-dynamic`을 설치합니다.

## 빌드와 테스트

Debug를 빌드합니다.

```bat
.\build_dev.bat
```

Release를 빌드합니다.

```bat
.\build_release.bat
```

### 빌드 완료 알림

`build_dev.bat`와 `build_release.bat`는 구성, 컴파일, 런타임 파일 확인, 라이선스 고지 복사를 마치면 알림을 보냅니다.

Windows 토스트 알림을 사용하려면 Windows PowerShell(`powershell.exe`)을 열고 선택 모듈 [`BurntToast`](https://www.powershellgallery.com/packages/BurntToast)를 현재 사용자에게 설치합니다.

```powershell
Install-Module -Name BurntToast -Scope CurrentUser
Get-Module -ListAvailable -Name BurntToast
```

스크립트는 `powershell.exe`에서 모듈을 찾아 `New-BurntToastNotification -Text 'Build finished'`를 호출합니다. 해당 PowerShell 환경에서 모듈을 확인합니다.

`BurntToast`가 없으면 Windows [`msg.exe`](https://learn.microsoft.com/windows-server/administration/windows-commands/msg)로 `%USERNAME%`에게 `build finished`를 최대 10초간 표시합니다.

등록된 Debug 테스트를 실행합니다.

```bat
ctest --test-dir build --output-on-failure
```

엄격한 검증은 Debug 빌드 디렉터리를 다음과 같이 구성합니다.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`KOMAPEDIT_STRICT_WARNINGS`의 기본값은 꺼짐입니다. CTest는 `multilanguage_contract`, `typed_snapshot_contract`, `maploader_gradient_projection_contract`, `typed_edit_contract`, `maploader_diagnostics_contract`, `canvas3d_camera_contract`, `route_value_sampling_contract`의 일곱 계약을 등록합니다.

테스트 실행 파일과 headless 구현은 Debug에서만 컴파일합니다. `build_dev.bat`는 `BUILD_TESTING`을 켜며 `build_release.bat`는 테스트를 끄고 출력 폴더에 이전 `*_tests.exe`가 있는지 확인합니다. headless 명령은 별도로 실행합니다. 진단 계약은 Git이 무시하는 `tests/`의 로컬 픽스처를 사용합니다. 실행 전에 확인하세요.

`build\bin\typed_snapshot_tests.exe slop`은 객체 키 식, 희소 Section 인덱스, 유한한 거리와 패치 미리 보기의 회귀 테스트를 실행합니다. 해당 사례는 스냅샷 및 편집 계약에도 포함됩니다.

`build\bin\typed_snapshot_tests.exe patch-bench 7`은 100개, 1,000개, 10,000개의 결정적 수정 작업을 전체 모의 실행해 `plan.patch_sources`를 측정합니다. 각 크기에서 한 번 예열하고 표본 전체와 중앙값/p95를 보고합니다. 반복 기본값은 7, 범위는 5–50입니다. 이 벤치마크는 명시적으로 실행합니다.

`build\bin\typed_snapshot_tests.exe distance-plan-bench 4000 middle`은 거리 기준점이 1,000개, 2,000개 또는 4,000개인 합성 맵에서 단일 삽입을 측정합니다. 대상에 `middle` 또는 `eof`를 지정합니다. 프로세스마다 한 번 예열하고 다섯 번 모의 실행을 측정합니다. `plan.prepare`와 전체 중앙값/p95, 작업 집합 최대 바이트를 보고하고 메모리 적용·초기화와 디스크 바이트 보존을 확인합니다.

런타임 출력 배치는 다음과 같습니다.

- Debug는 `build\`, Release는 `build_release\`를 사용합니다.
- 출력 최상위에는 `komapedit.exe`, `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`가 있습니다.
- `bin\`에는 `maploader.dll`, `model_loader.dll`, Assimp와 복사한 런타임 DLL이 있습니다.
- `settings\`에는 생성된 `settings.ini`, `history.ini`, `imgui.ini`가 있습니다.
- 빌드 및 배포 정리 스크립트는 출력 최상위에서 구형 INI나 DLL을 발견하면 중단하고 수동 처리 방법을 표시합니다.

빌드 디렉터리, 복제한 `third_party` 트리, 생성한 설정·CSV·테스트 결과와 임시 노선·맵·모델 픽스처는 Git 추적 대상에서 제외합니다.

## 소스 구성

| 영역 | 주요 파일과 역할 |
| --- | --- |
| 공개 맵 ABI | `include/maploader.h`, `include/maploader_snapshot.h`. API v13 및 맵 스냅샷 v9 함수, 고정 폭 POD 스냅샷, Scenario v2 스냅샷 및 v2 직접 저장 초안, 편집 배치, 보고서, 범위, 소유권, 버전과 구조체 크기 |
| 맵 수명 주기 | `src/maploader/maploader.cpp`. C ABI 진입점, 핸들, 재구축, 디스패치, 원본 접근, 경계의 오류 처리 |
| 맵 상태 | `maploader_internal.h`. `MapContext`, 파싱한 행, 원본 범위, Include 스택, 편집 참조, 보고서, 시간 측정 |
| 파싱 | `maploader_core.cpp`, `maploader_parser.cpp`, `text_decoder.cpp/.h`. 구문, 값, Include, 변수, 인코딩, 원본 기준점, 중복 확인 |
| 시나리오 파싱과 직접 저장 | `scenario_route.cpp/.h`. BVE 파일 종류 판별, 공식 시나리오 전체 스냅샷 스키마, 원본을 보존하는 직접 저장, 시나리오에서 맵을 열기 위한 `Route` 후보 해석 |
| 선형 | `maploader_geometry.cpp`. 기준 궤도와 다른 궤도의 선형, 위치 변환, 곡선, 구배, 배치 버퍼와 장면 제어점 |
| 식별과 스냅샷 | `maploader_identity.cpp`, `maploader_snapshot.cpp`, `maploader_semantic.cpp`. 안정적 ID, 타입 스냅샷, 리비전, 비교와 지문 |
| 편집 | `maploader_edits.cpp`. 모의 실행, 메모리 적용, 직접 적용, 커밋, 초기화, 원본 패치, 인코딩을 보존하는 쓰기, 거리 변경. 제한된 Include 경로 수정은 기존·새 하위 트리를 마스킹한 전체 재파싱으로 검증 |
| 공통 연결 | `include/repeater_linkage.h`, `include/own_track_transition_linkage.h`. 반복 구조물 연결과 곡선·구배 완화 구간의 짝짓기 |
| 공통 노선 값 샘플링 | `include/route_value_sampling.h`, `src/main_window/route_value_sampling.cpp`. 계산된 이벤트의 안정적 순서, BeginTransition/Interpolate 구간 분류, 생략 값 상속, 2D/3D 공통 끝점 거리 |
| 수치 안전성과 편집 시간 | `include/numeric_safety.h`. GUI·장면에서 double을 정수로 변환할 때 유한성·범위 검사. `include/operation_timing.h`. GUI와 maploader 영역을 분리한 선택적 steady-clock 포함 시간 측정 |
| 모델 가져오기 | `src/model_loader/model_loader.cpp`, `include/model_loader.h`. Assimp 격리와 model-loader API v2 |
| 기본 창 | `gui_kme.cpp`, `kme.h`와 `src/main_window/`의 담당 모듈. App 상태 조정, Win32/D3D11 시작, 공통 도구·배경 이미지, 스냅샷 반영·불러오기, 편집·거리·속성 창·목록 초안·새 파일·맵 요소 작업, 대화 상자·UI, 장면 미리 보기, headless 진입점. 새 파일 UI는 `new_element_wizard.cpp`, 내용·대화 상자 도우미는 `app_dialogs.cpp`, 지연 생성은 `element_inspector_data.cpp`, 작업 계약은 `headless_entrypoints.cpp` |
| 런타임과 설정 | `app_settings.cpp/.h`, `runtime_paths.cpp/.h`, `maploader_runtime.cpp`. INI 파일, 실행 파일 기준 경로, DLL 불러오기, API 버전 일치 검사 |
| 원본 도구 | `file_structure_diagram.cpp`, `text_preview.cpp`. Include 그래프, 작업 사본 미리 보기, Include 파일 교체 또는 참조 제거, 거리 구문 경계 선택 |
| Debug 검증 | `debug_headless.cpp/.h`, `headless_entrypoints.cpp`, `edit_benchmark.cpp`, `src/table/datatable_benchmark.cpp`, `touch_input.cpp/.h`. 인수 파싱, headless 계약, 실제 작업 절차, 편집·표 캐시 벤치마크, 카메라 전환, 찾기, 터치, 편집, 파일 생성 검사 |
| 2D 보기 | `src/canvas2d/canvas2D.cpp`의 평면도·종단면도 데이터, `Curve.Interpolate` 끝점 마커 캐시 반영, 평면도 렌더링. `canvas2d_view_state.cpp/.h`의 이동·확대·회전·좌표 변환. `canvas2d_marker_cache.cpp/.h`의 궤도 샘플링과 마커·Repeater 오버레이 캐시. `canvas2d_interaction.cpp/.h`의 측정·마커 히트, 상황에 맞는 작업과 원본 매핑. `canvas2d_background.cpp/.h`의 이미지 좌표, 그리기와 두 점 정렬. `canvas2d_primitives.cpp/.h`의 화면 변환, 폴리라인 클리핑, 격자, 축척, 마커. `profile_plots.cpp`의 표고·반경 차트 |
| 3D 보기 | `include/canvas3D.h`와 `src/canvas3d/canvas3D.cpp`의 공개 미리 보기 인터페이스와 위임. 내부 상태는 `canvas3d_impl.h`, 구현은 아래 `canvas3d_*.cpp` 모듈. `scene_track_sampling.cpp/.h`와 `scene_route_overlay.cpp/.h`는 CPU 샘플링과 노선 정보 서식 |
| 표와 탐색 | `src/table/datatable*.cpp` 모듈, `datatable_internal.h`, `table_navigation.cpp`. 공통 셀·열, 단일 캐시 반영 경로, 찾기, 목록 인라인 편집, 노선·효과·시나리오 창, Debug 벤치마크, 행·평면도·장면 사이 이동 |
| 공통 마커 | `include/map_marker_visuals.h`, `map_marker_visuals.cpp`. 2D/3D 마커의 공통 시각 정의 |
| 현지화 | `include/multilanguage.h`. 중국어 간체, 중국어 번체(대만), 중국어 번체(홍콩), 영어, 일본어, 한국어 UI 텍스트 |

원본 소유권, 연결 관계, 마커, 탐색, 파싱, 검증과 쓰기는 담당 구성 요소의 경계와 공통 구현을 따릅니다.

## 코드 상세 설명

`include/`와 `src/`의 주요 파일을 설명합니다. GUI는 C ABI로 두 DLL을 호출합니다. 파서는 원본 식별 정보를 관리하고, 스냅샷은 ABI 데이터 뷰를 제공하며, 편집기는 원본 패치를 생성·검증·커밋합니다.

### 공개 ABI, 공통 알고리즘, 리소스 헤더

#### `include/maploader.h`

- **내보내기와 로그**에서 `KV_API`는 DLL export/import를 제어합니다. `KvLogCallback`과 `kv_set_log_callback()`은 호스트에 영어 진단을 전달합니다. `kv_api_version()`의 ABI 버전은 EXE와 정확히 일치해야 합니다.
- **시나리오 인터페이스**의 `kv_probe_file_kind()`는 맵과 시나리오를 구분합니다. `kv_resolve_scenario_routes()`는 대상 파일이 있는 Route 후보를 반환하며 `kv_free_scenario_candidates()`로 해제합니다. `kv_load_scenario_snapshot()`은 독립 v2 스냅샷을 반환하며 `kv_free_scenario_snapshot()`으로 해제합니다. v2 `kv_save_scenario_document()`는 원본 해시를 검사하고 변경을 모두 재파싱한 뒤 원래 인코딩으로 트랜잭션 쓰기를 수행합니다. 후보의 수와 순서 변경, 빠진 Vehicle 삽입, 빈 Vehicle 값을 지원합니다.
- **핸들과 선형**에서 `kv_load_map_ex()`는 `KV_LOAD_PREVIEW`와 `KV_LOAD_EDIT_METADATA`로 미리 보기 또는 편집 메타데이터를 선택합니다. `kv_generate_geometry()`와 `kv_generate_scene_geometry()`는 일반 궤도 및 장면 궤도 데이터를 생성하고 핸들의 각 캐시와 리비전을 갱신합니다.
- **읽기 전용 스냅샷**을 반환하는 `kv_get_map_snapshot()`과 `kv_get_scene_geometry_snapshot()`은 버전과 구조체 크기를 검사합니다. 재파싱 또는 관련 선형 무효화 전에 호출자가 필요한 데이터를 복사합니다. 중첩 저장소는 핸들이 관리합니다.
- **편집과 원본 접근**의 `kv_get_edit_target_typed()`는 안정적 편집 ID의 필드와 원본 정보를 가져옵니다. `kv_get_source_text()`는 디스크 또는 메모리 오버라이드에서 디코딩한 텍스트를 반환합니다. `kv_edit_dry_run_typed()`, `kv_edit_apply_to_memory_typed()`, `kv_edit_apply_typed()`, `kv_edit_commit_typed()`, `kv_edit_reset_memory()`는 각각 검증, 작업 사본 적용, 직접 쓰기, 작업 사본 커밋, 오버라이드 폐기를 담당합니다.
- **오류와 해제**에서 `kv_get_last_error()`는 스레드 로컬 오류를 반환합니다. `kv_free()`는 맵 핸들을, `kv_free_string()`은 DLL에서 독립적으로 할당한 문자열을 해제합니다.

#### `include/maploader_snapshot.h`

- **버전과 공통 뷰**를 정의합니다. API/스냅샷 버전, `KV_INDEX_NONE`, 기능 비트, 편집 플래그가 있습니다. `KvUtf8View`는 호출 동안만 유효한 UTF-8 입력입니다. `KvStringRef`, `KvSpan`, `KvDoubleBuffer`는 스냅샷 메모리 영역과 배열의 고정 폭 뷰입니다.
- **시나리오 스냅샷**의 `KvScenarioPathWeightRow`는 상대 경로, 가중치, 명시 가중치 플래그를 담습니다. v2 `KvScenarioSnapshot`은 원본 해시와 여덟 필드의 존재 비트를 보관합니다. `KvScenarioEditDocument`와 `KvScenarioEditPathRow`는 `kv_save_scenario_document()` 호출 동안 유효한 입력입니다.
- **값과 원본 식별**에서 `KvValueKind`와 `KvValue`는 null, 수치, 문자열, continue를 표현합니다. `KvSourceFileRow`, `KvSourceSpanRow`, `KvStatementRow`, `KvElementRow`, `KvRowMetadata`는 물리 파일, Include 스택, 바이트·줄·열 범위, 원본 인수, 파싱 순서, 안정적 편집 ID를 보존합니다.
- **타입 행**에는 `KvTrack`, `KvStation*`, `KvStructure*`, `KvRepeater*`, `KvSignal*`, `KvSection*`, `KvSound*`, `KvOtherTrain*`와 곡선, 구배, 속도 제한 및 환경 효과의 POD 레코드가 있습니다. 가변 인수는 `KvSpan`으로 공통 값 배열을 참조합니다.
- **루트 스냅샷**인 `KvMapSnapshot`은 문자열 영역, 값, 타입 행 배열, 선형 행렬, 원본 등록부, 기능 비트, 콘텐츠 및 선형 리비전을 모읍니다. `KvSceneGeometrySnapshot`은 별도의 무효화 수명 주기로 장면 제어점과 궤도 행렬을 관리합니다.
- **편집 프로토콜**에서 `KvEditField`와 `KvEditTargetSnapshot`은 편집 가능한 필드를 기술합니다. `KvEditOperation`, `KvEditChange`, `KvEditBatch`는 삽입·수정·삭제 요청을 표현합니다. 거리 위치 결정, 패치 미리 보기, 커밋한 파일과 행, `KvEditReportSnapshot`은 검증과 커밋 결과를 기술합니다. 요청과 스냅샷은 공개 헤더의 버전 및 구조체 크기를 확인합니다. 필드 변경은 EXE와 DLL에 함께 반영합니다.

#### `include/model_loader.h`

- `MlVertex`는 위치, 법선과 UV를, `MlMaterial`은 확산색과 텍스처 경로를 담습니다. `MlMeshPart`는 인덱스 구간과 재질을 연결하고 `MlMeshData`는 정점, 인덱스, 재질, 파트, 경계·중심·반경을 모읍니다. `ml_api_version()`이 반환하는 v2는 EXE의 요구와 정확히 일치해야 합니다.
- `ml_api_version()`, `ml_load_model()`, `ml_free_model()`, `ml_get_last_error()`가 전체 C ABI입니다. DLL이 모든 배열을 할당하며 성공 또는 부분 실패 후 `ml_free_model()`로 해제합니다.

#### `include/repeater_linkage.h`

- `EventKind`는 `Begin`/`Begin0`을 Begin으로 분류하고 End 및 기타 이벤트를 구분합니다. `BoundaryKind`는 명시적 End, 후속 Begin, 열린 경계를 구분합니다. `Event`는 거리, 전체 파싱 순서, 반복 구조물 키, 원본 행 인덱스를 보존합니다.
- `canonical_key()`는 대소문자를 구분하지 않는 키 비교를 표준화합니다. `pair_linkage()`는 거리, 전체 파싱 순서, 원본 행 인덱스로 안정 정렬하고 키별 활성 연결을 관리해 `Chain`과 `Segment`를 생성합니다. 새 Begin은 이전 구간을 닫고 End는 연결 전체를 닫습니다. 같은 거리에서는 마지막에 파싱한 이벤트가 활성 상태를 정합니다.
- `pair_segments()`는 스냅샷, 표, 2D, 3D가 공통으로 쓰는 일렬의 구간 목록을 제공합니다.

#### `include/own_track_transition_linkage.h`

- `EventKind`는 곡선·구배 `BeginTransition` 이벤트와 연결될 Begin/End를 나타냅니다. `Pair`는 완화 구간 시작 행과 연결 대상 행의 인덱스를 저장합니다.
- `consumes_curve_transition()` 및 `consumes_gradient_transition()`은 대기 중인 완화 구간을 이어받는 구문을 정의합니다. `pair_transitions()`는 소스 순서대로 곡선과 구배의 대기 상태를 각각 추적하여 짝과 짝이 없는 구문을 반환합니다. 편집과 마커는 이 결과로 완화 구간과 대상 구문을 연결합니다.

#### `include/map_marker_visuals.h`

- `MapMarkerVisualKind`는 2D/3D 공통 요소의 시각 유형을 정의합니다. `map_marker_visual_bit()`는 이를 표시 비트로 변환합니다.
- `MapMarkerPrimitiveKind`, `MapMarkerColorRole`, `MapMarkerIconVariant`는 기본 도형, 색상의 역할, 변형을 정의합니다. `MapMarkerIconPrimitive`와 `MapMarkerIconRecipe`는 정규화된 점, 선 너비, 닫힘·채움 플래그, 글리프를 저장합니다.
- `map_marker_theme_color()`, `map_marker_role_color()`, `map_marker_icon_recipe()`, `draw_map_marker_icon()`은 2D/3D에서 색상, 아이콘 정의, ImDrawList 인터페이스를 공유합니다.

#### `include/route_value_sampling.h`와 `src/main_window/route_value_sampling.cpp`

- `Event`는 maploader에서 받은 노선 계산값과 이벤트 유형을 안정적인 거리 순서로 보관합니다. `append_event()`는 일반 값, `BeginTransition`, `Interpolate`를 구분합니다. 인수 없는 Interpolate는 이전 값을 상속합니다.
- `sample()`은 2D와 3D에서 공통으로 사용합니다. 상수 구간은 현재 값을, 완화·보간 구간은 양 끝점의 거리와 값을 반환합니다.

#### `include/numeric_safety.h`와 `include/operation_timing.h`

- `kme::truncating_int_or_zero()`는 `int` 범위 안의 유한한 double의 소수부를 버립니다. 유한하지 않거나 범위를 벗어난 값에는 `0`을 반환합니다. GUI와 장면 코드는 이 변환 경계를 공유합니다.
- `kme::timing::Timing`은 명시적으로 활성화하는 스레드 로컬 단계별 포함 시간 측정을 제공합니다. GUI와 maploader는 별도 영역에서 단계별 밀리초와 횟수를 누적하여 콘솔에 표시합니다.

#### `include/canvas3D.h`

- **장면 입력**의 `Canvas3DTrackPoint/Path/Visibility`는 궤도 표본과 표시 상태를 기술합니다. `Canvas3DSceneObject`, `Canvas3DModelInstance`, `Canvas3DRepeaterSegment`, 배경·안개·표시 거리 이벤트가 장면 입력을 이룹니다.
- **노선 정보와 마커**에는 카메라 거리에서 샘플링할 `route_value_sampling::Event`, 역, 제한 속도, 폐색 구간 신호 이벤트가 있습니다. `Canvas3DSceneMarker`는 시각 유형, 거리, 궤도 위치, 표 대상과 편집 ID를 저장합니다. `Canvas3DSceneMarkerVisibility`는 유형별 비트로 인덱스 재구축을 제어합니다.
- **빌드 및 갱신 구조체**의 `Canvas3DScene`은 렌더러와 독립적인 CPU 장면 기술입니다. `Canvas3DSceneBuildOptions/Result` 및 `Canvas3DSceneMapRefreshOptions`는 초기 빌드, 동적 콘텐츠 갱신, 맵 콘텐츠 갱신을 구분합니다. `Canvas3DSceneStats`는 인스턴스, 모델, 프레임률 통계를 제공합니다.
- **조작 구조체**의 카메라 자세, 상황별 작업, 선택 대상, `Canvas3DPlacementEditTarget`, 드래그 축, `Canvas3DPlacementDragUpdate`는 렌더러 조작을 GUI에서 적용할 수 있는 원본 필드 변경으로 바꿉니다.
- **`Canvas3D` 파사드**는 단일 모델과 장면 불러오기, 갱신, 표시 여부, 표시 거리, 안개, 기즈모, 카메라 조작, 성능 경고와 디버깅을 제공하고 내부 PImpl에 구현을 위임합니다.

#### `include/multilanguage.h`

- `Language`는 일본어, 영어, 중국어 간체, 중국어 번체(대만), 중국어 번체(홍콩), 한국어를 선택합니다. `Translation`의 여섯 필드는 같은 의미의 UI 텍스트를 담습니다. `Language::ZhTw`는 `zh_tw`, `Language::ZhHk`는 `zh_hk`, `Language::Ko`는 `ko`를 선택합니다. 번역 상수는 창, 메뉴, 도구 모음, 표, 속성 편집, 오류, 2D/3D 조작별로 묶습니다.
- `tr()`와 언어 선택 도우미는 활성 언어의 필드를 반환합니다. 같은 `Translation` 초기화 코드에 여섯 언어의 번역을 함께 추가하고 서식 자리표시자를 일치시킵니다. `zh_tw`는 대만 용어(행 列, 열 欄), `zh_hk`는 홍콩 용어(행 行, 열 欄, 셀 單元格)를 사용합니다. 홍콩 번역은 중국어 간체 문구를 기준으로 하고 필요하면 일본어와 영어 용어를 참조합니다.
- 기존 `[General] language`는 `ja`, `en`, `zh`, `zh-TW`, `zh-HK` 또는 `ko`를 저장합니다. 기본값은 중국어 간체입니다. `中文` 하위 메뉴에 `简体`, `台湾繁體`, `香港繁體`가 있으며 `English`, `日本語`, `한국어`와 같은 단계에 놓입니다.
- `multilanguage_contract`는 키 집합, 비어 있지 않은 값, 자리표시자 이름과 개수, 언어 조회와 주요 용어를 확인합니다. 주 글꼴은 전체 중국어 글리프 범위를 유지하며, 후보에 Segoe UI 앞의 Microsoft JhengHei(`msjh.ttc`)가 포함됩니다. 시작할 때 주 글꼴과 기호 글꼴 뒤에 Windows의 맑은 고딕(`malgun.ttf`)을 병합합니다. MSVC는 GUI와 번역 테스트를 `/utf-8`로 컴파일합니다.

#### `include/resource.h`

- Windows 리소스 컴파일과 C++에서 리소스 ID를 공유합니다. `IDI_KOMAPEDIT`는 `komapedit.rc`의 애플리케이션 아이콘을 식별합니다.

### maploader 상태, 파싱, 스냅샷

#### `src/maploader/maploader_internal.h`

- **유틸리티와 시간 측정**의 문자열 도우미, `SteadyClock`, `LoadTiming`, `ScopedTimer`, `ActiveTimingScope`는 읽기, 파싱, 병합, 선형, 스냅샷의 시간을 잽니다. 작업 슬롯 선언은 비용이 큰 불러오기의 동시 실행을 제한합니다.
- **디코딩과 파싱 옵션**의 `LoadedText`는 원래 바이트, UTF-8 내용, 인코딩, BOM, 줄바꿈, 줄 시작 위치를 보존합니다. `MapParseOptions`는 미리 보기·편집 메타데이터 수준을 선택합니다. `SourceTextOverride(s)`는 작업 사본 오버라이드를 담습니다.
- **식의 값**을 다루는 `ValueKind`, `Value`, `VariableEnvironment`는 파싱 시의 null, 수치, 문자열 값과 변수 바인딩을 나타냅니다. 구문에 공유 불변 환경 스냅샷을 붙여 거리 이동의 의미를 검사합니다.
- **원본 모델**의 `SourceFileRecord`, `FileStructureRecord`, `SourceSpan`, `ParsedStatement`, `EditSourceRef`, `MapDiagnostic`은 물리 파일, Include 호출의 식별 정보, 원래 구문, 줄·열·바이트 기준점, 파싱 순서와 편집 식별 정보를 보존합니다.
- **파싱한 행**의 `CurveEditRow`, `GradientEditRow`, `OtherTrackChange`와 역, 구조물, 반복 구조물, 신호, 폐색 구간, 소리, 열차, 속도 제한, 효과 레코드는 선형, 스냅샷, 편집에 타입 데이터를 제공합니다. 행별 `EditSourceRef`는 원본 쓰기에 사용합니다.
- **행렬과 스냅샷 저장소**에서 `Matrix`는 연속된 행·열 double 버퍼를 관리합니다. `MapSnapshotStorage`, `SceneGeometrySnapshotStorage`, `EditTargetSnapshotStorage`, `EditReportSnapshotStorage`는 ABI 포인터 뒤의 벡터와 문자열 영역을 소유합니다.
- **`MapContext` 집합**은 시작 경로, 원본·Include 표, 변수 환경, 모든 파싱 행, 궤도·장면 행렬, 제어점, 리비전, 스냅샷 캐시, 작업 사본 오버라이드, 디스크 기준 해시, 최신 편집 보고서와 시간 정보를 소유합니다. 파싱, 선형, 스냅샷, 편집의 소유권 루트입니다.
- **활성 구문과 편집 RAII**에서 `ActiveStatementScope`는 디스패치 중 활성 구문과 원본 환경을 설정하고 나갈 때 복원합니다. `MapEditChange`와 전용 필드 구조체는 복사한 ABI 요청을 담습니다. 의미 스냅샷, 거리 위치 결정 레코드, 패치·커밋 보고서는 트랜잭션 검증에 쓰입니다.
- **내부 선언**은 헤더 끝에 있으며 DLL 내부의 파싱, 선형, 스냅샷, 의미 비교, 편집, 식별 진입점을 정의합니다.

#### `src/maploader/text_decoder.h`

- UTF-8과 `std::filesystem::path` 사이의 변환, 바이너리 읽기, UTF-16과 코드 페이지 디코딩, BOM·첫 줄 탐지, 인코딩을 보존하는 쓰기를 선언합니다.
- `FileOpenFailureKind`는 파일 없음, 권한 실패, 디렉터리, 일반 열기 실패를 구분해 파서와 모델 로더가 일관되게 진단하도록 합니다. 쓰기는 대상 인코딩과 BOM을 받아 표현할 수 없는 문자를 만나면 실패합니다.

#### `src/maploader/text_decoder.cpp`

- `classify_file_open_failure()`, `file_open_failure_message()`, `read_binary_file()`은 읽기와 Windows 오류 분류를 처리합니다. `path_to_utf8()`, `utf8_to_wide()`, `wide_to_utf8()`, `path_from_utf8()`, `join_utf8_path()`는 Win32 와이드 경로 처리를 모읍니다.
- `decode_codepage()`는 Windows 코드 페이지를 엄격 또는 관대한 모드로 변환하고, 다른 플랫폼에는 제한적인 대체 경로를 제공합니다. `append_utf8_codepoint()`와 `decode_utf16()`은 바이트 순서, 서로게이트 쌍, 유효하지 않은 시퀀스를 처리합니다.
- `decode_text_bytes()`는 선언된 인코딩과 BOM으로 UTF-8, UTF-16, CP932 중 하나를 고릅니다. `first_line_ascii()`와 `has_utf8_bom()`은 전체 디코딩 전에 맵 헤더를 식별합니다.
- `append_utf16_bytes()`와 `encode_text_for_writeback()`은 UTF-8 작업 사본을 원래 인코딩으로 변환하고 BOM을 보존합니다. 표현할 수 없는 문자는 예외를 발생시킵니다.

#### `src/maploader/maploader_core.cpp`

- **작업, 시간 측정, 스칼라**에서 `try_acquire_maploader_task_slot()`/`release_maploader_task_slot()`은 동시 불러오기를 제어합니다. `ActiveTimingScope`는 활성 시간 합계를 기록합니다. `ascii_lower()`, 공백 제거, `parse_finite_number()`, `canonical_number()`, 버전·인코딩 헤더 파싱은 공통 스칼라 규칙을 제공합니다.
- **텍스트 불러오기**의 `build_line_starts()`, `detect_newline()`, `make_loaded_header_text()`와 두 `load_header_text()` 오버로드는 원래 바이트를 인코딩, 줄바꿈, 본문 위치가 있는 `LoadedText`로 바꿉니다. 메모리 오버라이드가 우선합니다.
- **값과 키 변환**의 `as_number()`, `as_text()`, `key_text()`, `track_key_display_text()`, `track_key_from_display_text()` 및 CSV/INI 도우미는 파서 값, 표 의미와 편집 텍스트 표현을 통일합니다.
- **원본 등록과 위치**에서 `normalized_source_path/key()`, `current_source_text()`, `register_source_file_index()`와 Include 스택·호출 키의 재사용은 원본 식별 정보의 중복을 제거합니다. `line_column_for_body_pos()`와 `make_source_span()`은 본문 오프셋을 안정적인 물리 기준점으로 바꿉니다.
- **구문과 환경**의 `current_variable_environment_snapshot()`, `rebuild_variable_environment_snapshot()`, `add_parsed_statement()`, `next_active_edit_ref()`는 구문, 환경, 편집 참조를 등록합니다. 병합·오프셋 도우미는 유효한 인덱스를 유지하며 Include 문맥을 합칩니다.
- **목록 행 원본 레코드**의 `add_loaded_line_statement()`와 `extend_loaded_line_statement()`는 물리 CSV·목록 행의 편집 가능한 구문을 등록합니다. `parse_signal_aspect_source_values()`와 필드 이름 도우미는 가변 구조물 키 열과 눈부심 행 형태를 보존합니다.
- **의존성과 상태 갱신**에서 `value_equal()`, 변수 읽기·쓰기 레코드와 `log_load_timing()`은 의미 검증과 성능 로그에 사용합니다. `add_controlpoint()`, `set_distance()`, `put_own()`, `ensure_othertrack()`, `put_other()`는 파서 디스패치에서 궤도 이벤트 상태를 갱신하는 공통 진입점입니다.

#### `src/maploader/maploader_parser.cpp`

- **렉서와 구문 루프**에서 `Parser::parse()`가 파일을 처리하고 `eof()`, `peek()`, `skip()`, `accept()`, `expect()`가 공백, 주석, 구두점을 처리합니다. 진단에 위치를 남기고 `finish_statement()`/`synchronize_statement()`로 다음 구문에서 복구합니다.
- **객체, 함수, 식**의 `parse_label()`, `parse_variable_name()`, `parse_map_object()`, `parse_map_function()`, `parse_map_args()`는 `MapObject`와 `MapFunction`을 만듭니다. `parse_expression()`, `parse_prefix()`, `parse_primary()`, `apply_binary()`, `call_function()`은 우선순위, 변수, 문자열, 숫자, 지원되는 수학 함수를 구현합니다.
- **Include 절차**의 `include_path_is_simple_string()`은 미리 보기 경로를 검사합니다. `make_child_seed()`는 일반 변수와 Include 식별 정보를 상속하고 자식의 거리를 0, 거리 식을 빈 값으로 시작합니다. `parse_include_context()`는 병렬 파싱을 지원합니다. `queue_include()`와 `flush_pending_includes()`는 원래 순서로 원본, 진단, 이벤트와 변수 쓰기를 병합합니다. 상위 파일의 거리를 보존하고 오래된 변수 의존성은 다시 파싱합니다. 난수 엔진은 프로세스 세션 시드, 원본 경로, Include의 소스 순서에서 파생됩니다. 재파싱은 큐의 시드를 유지하므로 Preview/Edit 불러오기와 작업 사본 재파싱에서 바뀌지 않은 `rand()` 호출을 재현할 수 있습니다.
- **구문 검증과 디스패치**의 `method_rules()`는 인수 개수와 null 규칙을 정의합니다. `object_path()` 및 `validate_statement()`는 일반 구문을 검증하고 `dispatch()`는 최상위 객체를 전용 처리기로 보냅니다. `record_deferred_semantics()`는 리소스 목록을 불러온 뒤 확인할 키를 기록합니다.
- **기준 궤도와 다른 궤도**에서 `dispatch_curve()`, `dispatch_gradient()`, `dispatch_legacy()`는 곡선, 구배, 구형 이벤트를 기록합니다. `dispatch_track()`, `setposition_interpolate()`, `track_position()`은 다른 궤도의 위치, 보간, 궤간, 중심, 캔트 이벤트를 기록합니다.
- **리소스 목록**의 `load_resource_list()`와 `record_resource_list_load()`는 Load 식, 계산한 경로, 원본 식별 정보를 보존합니다. `parse_station_list()`, `parse_structure_list()`, `parse_signal_aspect_list()`, `parse_sound_list()`는 물리 행을 편집 가능한 타입 레코드로 바꿉니다. `parse_other_train_file()`은 타 열차 파일을 읽습니다.
- **맵 요소 디스패치**의 `dispatch_station/speedlimit/section/signal/beacon/pretrain/structure/sound/train/repeater/irregularity/background/adhesion/cab_illuminance/fog/draw_distance()`와 소음 처리기 세 개는 메서드 형식을 검사하고 인수를 읽어 행을 추가합니다. `add_other_train_definition()`은 Train 정의를 등록합니다.
- **파싱 후 진단**에서 `validate_unique_preview_statements()`는 중복 Load/Enable을, `append_transition_diagnostics()`는 짝을, `append_deferred_key_diagnostics()`는 리소스 키를 확인합니다. 역에 도착음이나 출발음이 있으면 `append_station_sound_load_order_diagnostic()`은 `Sound.Load`가 `Station.Load`보다 앞서는지 검사합니다. 파일 안에서는 줄·열, 파일 사이에서는 Include 깊이, 같은 깊이에서는 전체 파싱 순서로 비교하며 순서가 어긋나면 영어 `[WARN]`을 남깁니다. `emit_diagnostics()`는 진단을 내보내고 오류를 불러오기 실패로 처리합니다.
- **모듈 진입점** `parse_map_context()`는 `MapContext`를 만들고 시작 파일을 불러온 뒤 Parser를 실행하고 환경과 진단을 갱신하여 문맥 전체를 반환합니다. 모든 불러오기와 편집 후 재파싱에 사용합니다.

#### `src/maploader/maploader_geometry.cpp`

- **궤도 상태 머신**의 `LastPos`는 이전 표본을 담습니다. `TrackPointer`는 기준 궤도 이벤트를 거리순으로 진행하며 일정 간격과 이벤트 경계에서 반경, 구배, 캔트, 방향, 좌표를 제공합니다.
- **곡선 계산**의 `rotate_xy()`, 가우스 적분, 프레넬 급수·점근식은 로컬 좌표를 적분합니다. `circular_curve*()`는 원곡선을, `halfsin_intermediate()`, `linear_transition_curve_local()`, `transition_curve*()`는 사인 반파장·선형 완화곡선을 계산합니다. 키와 해시 구조체는 반복되는 매개변수를 캐시합니다.
- **구배 투영**의 `constant_gradient_projection()`, `sinc()`, `gradient_transition()`은 노선 길이로 수평 투영과 표고를 계산합니다. `build_gradient_projection_samples()`와 `build_event_projected_distances()`는 이벤트 거리를 평면 위치에 대응시킵니다.
- **기준 궤도 생성**에서 `sort_unique()`와 `append_arange()`는 이벤트 표본과 정규 표본을 합칩니다. `generate_owntrack()`은 거리, XYZ, 방향, 반경, 구배, 캔트 및 관련 열을 쓰며 `generate_curveradius()`는 곡선 반경도 데이터를 만듭니다.
- **다른 궤도 생성**의 `relative_position()`은 곡선 기준 오프셋을 계산합니다. `CantProcessor`는 캔트 Begin/End/Interpolate를 처리합니다. `build_othertrack_buffer()`는 위치, X/Y 보간, 궤간, 중심, 캔트를 합쳐 기준 궤도에 맞춘 행렬과 유효성 데이터를 만듭니다.
- **좌표 이동과 배치**의 `relocate()`는 선형을 안정적인 로컬 좌표로 옮깁니다. `build_structure_put_buffer()`는 구조물 배치에 거리별 변환을 제공합니다.
- **적응형 장면 제어점**의 각도·행렬 도우미와 `build_scene_adaptive_controlpoints()`는 이벤트, 모델 span, 곡률, 구배와 요청 범위에 맞추어 밀도가 달라지는 제어점을 만듭니다.
- **진입점** `generate_geometry()`는 기준 궤도, 곡선 반경 데이터, 다른 궤도, 배치 버퍼, 장면 제어점을 생성하고 시간, 기능, 스냅샷 리비전을 갱신합니다.

#### `src/maploader/maploader_identity.cpp`

- `stable_hash64()`는 결정적 64비트 해시, `hex64()`는 고정 형식의 16진 문자열을 제공합니다. `edit_kind_token()`은 행 종류를 정규화합니다.
- `make_edit_id()`는 정규화한 원본 키, 전체 파싱 순서, 구문 유형, 로컬 순번으로 안정적 ID를 만듭니다. `statement_edit_id()`는 구문 ID를 캐시합니다. `native_element_edit_id()`와 `element_edit_id()`는 원래 행과 파생 식별 정보의 공통 진입점입니다.

#### `src/maploader/maploader_snapshot.cpp`

- `matrix_view()`와 `data_or_null()`은 연속된 내부 컨테이너를 ABI 뷰로 내보냅니다.
- `MapSnapshotBuilder::build()`는 루트 데이터, 궤도, 역, 구조물, 타 열차, 폐색 구간·신호·소리, 효과, 제작자 메시지, 미리 보기 행, 편집 등록부를 추가하고 `finalize()`로 스냅샷을 연결합니다.
- `string_ref()`는 공통 영역의 문자열을 재사용하고 새 텍스트를 추가합니다. `value()`/`append_values()`/`append_strings()`는 값과 범위를 만듭니다. `metadata()`는 `EditSourceRef`를 `KvRowMetadata`로 바꿉니다. 각 `add_*` 블록은 타입 필드를 복사하고 기능 비트에 따라 원본 정보를 붙입니다.
- `add_element(s)()`는 행 유형·편집 ID를 행 인덱스에 대응시킵니다. `bind()`는 벡터 크기가 확정된 뒤 원시 포인터를 연결합니다. `finalize()`는 버전, 구조체 크기, 개수, 리비전, 기능을 기록합니다.
- `invalidate_map_snapshot()` 및 `invalidate_scene_geometry_snapshot()`은 콘텐츠, 일반 선형, 장면 선형의 무효화 경계를 정의합니다. `build_map_snapshot()`과 `build_scene_geometry_snapshot()`은 필요할 때 캐시를 재구축합니다. `ordered_station_list_entries()`는 물리 역 목록 순서를 유지합니다.

#### `src/maploader/maploader_semantic.cpp`

- `SemanticWriter`는 타입과 값을 고정 순서로 쓰면서 해시를 계산합니다. `field()`, `value_span()`, `begin_element()`, `emit_element()`는 정규화한 의미 표현을 만듭니다.
- `changed_field()`와 숫자·문자열·값·궤도 키 읽기 함수는 스냅샷 값에 `MapEditChange`를 덧씌웁니다. 유효하지 않은 숫자나 빠진 필수 값은 거부합니다.
- `write_structure_model()`, `write_sound_list()`, `write_structure_put()`, `write_structure_between()`, `write_station_put/list()`, `write_signal_aspect/put()`, `write_repeater()`와 지상자, 소리, 소음, 배경, 점착, 안개의 `write_*` 처리기는 유형별로 유지하거나 변경해야 할 필드를 정의합니다.
- `write_curve()`, `write_gradient()`, `write_other_track_change()`는 메서드, 인수 개수, 짝을 보존합니다. `write_section_row()`는 가변 값 목록을 지원합니다. `reject_unknown_target_fields()`는 선언되지 않은 필드를 거부합니다.
- `build_semantic_map_snapshot()`은 보호 대상 요소를 순회하여 편집 ID별 의미 인덱스와 맵·환경 지문을 만듭니다. `expected_target_semantic()`은 수정·삭제의 기대 결과를 계산합니다. `FakeInsertSnapshotState`, `insert_semantic_container()`, `expected_insert_semantic()`은 삽입의 기대 결과를 만듭니다.

#### `src/maploader/maploader_edits.cpp`

- **ABI 입력 복사**의 `copy_utf8_view()`와 `copy_edit_batch()`는 구조체 크기, 포인터와 길이, 작업, 플래그, 중복 필드, UTF-8 뷰 수명을 검증한 뒤 호출 범위의 POD를 내부 `MapEditChange`에 복사합니다.
- **패치 위치**의 `load_source_patch()`는 작업 사본을 읽습니다. `source_range_in_text()`, `safe_statement_removal_range()`와 미리 보기 도우미는 이웃 주석과 구문을 보존하며 `SourceSpan`을 UTF-8 텍스트 범위로 바꿉니다.
- **거리 식**의 스캐너는 미리 정의된 `distance`, 최상위 덧셈·뺄셈, 안전하게 바꿀 수 있는 수치 항을 찾습니다. `find_safe_numeric_distance_addend()`, `apply_delta_to_distance_addend()`, `adjust_distance_expression_by_delta()`는 변수 식을 보존하고 가능한 경우 상수를 수정합니다. 그 외에는 거리 배치를 제안합니다.
- **인수와 CSV**에서 BVE 인수 분할·인용과 수치·선택 값·키 도우미는 바뀌지 않은 원본 인수를 보존합니다. CSV 파싱, 동등성 검사 및 `build_editable_csv_list_statement()`는 구분자, 끝 필드, 인코딩 표현 가능성을 유지합니다.
- **구문 생성기**의 `build_structure_model/sound_list/station_list/signal_aspect_statement()`는 목록 행을 다룹니다. `build_station_put/structure_put/signal_put/repeater_statement()`는 메서드와 인수 형식을 명시적으로 변환합니다. Structure/Repeater는 좌표 오프셋이 있는 형식과 없는 형식을 서로 바꿀 수 있습니다. 다른 `build_*` 함수는 메서드와 인수 형태를 유지하며 곡선, 구배, 다른 궤도, 폐색 구간, 제한 속도, 지상자, 소리·소음과 효과를 다룹니다.
- **대상과 대상 스냅샷**은 MapContext가 처음 조회할 때 만드는 인덱스를 사용합니다. 편집 ID마다 타입이 지정된 행이나 문의 위치를 저장합니다. `build_edit_target_snapshot()`은 필드, 원래 값, 원본 인수, 제약, `sourceHash`, `expectedSourceHash`를 반환합니다.
- **삽입 검증**의 `validate_insert_field_names()`, `validate_insert_method()`, `validate_insert_change()`는 행 종류, 메서드, 구조화된 필드를 검사합니다. `build_insert_statement()`는 이 데이터에서 일반 BVE 구문을 생성합니다.
- **거리 구간 계획**에서 `DistanceSectionAnalysis/PlanningIndex`는 물리 파일, Include 호출, 거리 구간을 인덱싱하여 첫 블록, 사이 구간, 마지막 블록, 파일 끝의 배치를 계획합니다. 경계가 명확한 마지막 구간에서 이동하면 그 방향으로 구간을 늘릴 수 있습니다. 삽입은 기존의 유일한 위치를 우선하며 위치가 모호하면 사용자가 선택합니다. 후보 필터는 변수와 물리 인스턴스 검사를 공유합니다. 선택한 계획은 적용이나 디스크 쓰기 전에 전체 의미를 검증합니다.
- **보고서와 트랜잭션 쓰기**의 `build_edit_report_snapshot()`은 패치, 위치 결정, 커밋 데이터를 뷰로 제공합니다. 해시와 임시 파일 도우미는 대상 옆에 준비 파일을 만듭니다. `replace_files_transactionally()`는 단계적으로 파일을 교체하며 실패하면 롤백합니다. `TransactionalWriteError`는 원래 오류와 롤백 오류를 함께 담습니다.
- **전체 의미 검증**의 `parse_report_candidate()`는 패치 오버라이드로 다시 파싱합니다. `validate_non_target_derived_state()`, `own_track_transition_state()`, `validate_edit_report()`는 대상 외 요소, 최종 변수 바인딩, 역 소유 관계, 완화 구간의 짝, 대상별 기대 의미를 비교합니다. 유효한 편집은 최종 현재 `distance`를 바꿀 수 있습니다.
- **배치 처리**의 `build_edit_report()`는 대상을 준비하고 물리 문맥과 대상 거리를 묶어 경계를 결정합니다. 수정·삭제·삽입을 만들고 겹침을 확인한 뒤 다시 파싱하여 검증합니다. 모의 실행, 메모리 적용, 직접 적용은 이 코어를 공유합니다.
- **작업 사본과 커밋**에서 `apply_patched_files_to_overrides()`, `reparse_context_with_overrides()`, `apply_edit_report_to_memory()`는 디스크 기준을 보존하며 오버라이드를 갱신합니다. `reset_memory_edits()`는 디스크 상태로 돌아가고 `populate_committed_edit_state()`는 저장된 식별 정보를 기록합니다. `commit_memory_edits()`는 모든 오버라이드를 다시 검사하고 트랜잭션으로 기록합니다.

#### `src/maploader/maploader.cpp`

- `parse_options_from_load_flags()`는 공개 플래그를 내부 프로필로 변환하고 알 수 없는 조합은 거부합니다.
- `kv_*` 내보내기 함수는 구현 호출 전에 핸들, 버전, 구조체 크기, 인수를 검사합니다. C ABI 경계에서 예외를 잡아 마지막 오류를 설정하고 실패 값을 반환합니다.
- `kv_load_map_ex()`는 `MapContext`를 만듭니다. 선형 진입점 두 개는 행렬과 리비전을 갱신하고, 스냅샷 진입점 두 개는 캐시된 뷰를 반환합니다. 편집 대상과 원본 텍스트 진입점은 작업 사본의 정보를 반환합니다.
- 시나리오 내보내기 함수는 `probe_bve_file_kind()`, `load_scenario_document()` 또는 `resolve_scenario_route_candidates()`를 호출합니다. 독립 할당한 스냅샷과 후보 블록은 DLL에서 소유하며 짝이 되는 해제 함수로 해제합니다.
- 모의 실행은 보고서를 만듭니다. 메모리 적용은 오버라이드를 만들고 설치하며, 초기화는 오버라이드를 폐기합니다. 직접 적용은 보고서를 트랜잭션으로 쓰고 커밋은 검증된 작업 사본을 저장합니다. `kv_free()`와 `kv_free_string()`은 DLL 할당에 대응합니다.

#### `src/maploader/scenario_route.h`와 `src/maploader/scenario_route.cpp`

- `probe_bve_file_kind()`는 파일 앞부분을 읽어 Map, Scenario, Unknown을 구분합니다. 읽을 수 없는 파일은 Unknown으로 두고 일반 불러오기 경로에서 자세한 오류를 보고합니다.
- `load_scenario_document()`는 선언한 인코딩으로 `BveTs Scenario 2.00`을 파싱합니다. `#`/`;` 주석을 처리하고 중복 필드는 마지막 값을 사용합니다. 공식 필드 여덟 개, 원본 해시와 존재 비트, 원본 기준 Route/Vehicle 경로, 가중치와 명시 여부를 보존합니다. Vehicle 값이 완전히 비어 있으면 경고를 출력하고 후보 0개로 존재 비트를 유지하며 유효한 Route 맵을 불러옵니다.
- `save_scenario_document()`는 디스크를 다시 읽고 기대 원본 해시를 검사합니다. 후보 개수가 같으면 마지막 유효 필드의 후보만 최소한으로 수정합니다. 개수가 바뀌면 해당 필드의 후보 값 전체를 다시 씁니다. 경로는 비어 있지 않아야 하며 예약 구문 문자를 피하고 유한한 양수 가중치를 사용합니다. 기존 Route에는 후보가 하나 이상 필요합니다. Vehicle은 후보를 0개 이상 받을 수 있습니다. 비어 있지 않은 초안은 누락된 Vehicle을 만들고, 후보 0개는 기존 값을 비웁니다. 공통 트랜잭션 쓰기 전에 전체를 다시 파싱합니다.
- `resolve_scenario_route_candidates()`는 파싱한 시나리오를 재사용해 해당 폴더를 기준으로 Route 경로를 해석하고 대상 파일의 존재를 확인합니다. 스냅샷 읽기는 선언한 데이터를 보존합니다.

#### `src/maploader/diagnostics.h`와 `src/maploader/diagnostics.cpp`

- 헤더는 로그 콜백, 마지막 오류, 정보·경고·오류 인터페이스를 선언합니다. 콜백은 원자적 접근으로 게시하며 각 호출 스레드는 `thread_local` 저장소에 마지막 오류를 둡니다.
- `emit_log()`는 로그 한 줄을 온전히 전달합니다. `log_info_at()`, `log_warn_at()`, `log_error_at()`은 심각도와 원본 파일 이름을 붙입니다. ABI의 catch 블록은 `set_last_error()`로 조회할 수 있는 오류를 저장합니다.

#### `src/maploader/c_api.h`와 `src/maploader/c_api.cpp`

- DLL 내부 도우미는 C 문자열의 소유권을 관리합니다. `copy_c_string()`은 `std::malloc()`으로 NUL 종료 UTF-8 텍스트를 공개 ABI에 복사합니다. `kv_free_string()`은 짝이 되는 `std::free()` 경로로 해제합니다.

### 모델 불러오기

#### `src/model_loader/model_loader.cpp`

- 경로·확장자 도우미는 Assimp 형식 힌트를 정규화합니다. `copy_c_string()`은 재질 텍스처 경로의 ABI 문자열을 할당합니다. `resolved_texture_path()`는 모델 기준 텍스처를 UTF-8 경로로 해석합니다.
- `free_mesh()`는 정점, 인덱스, 재질 문자열과 메시 파트를 반복 호출에도 안전하게 해제합니다. `MeshCleanupGuard`는 예외 시 정리하며 `assign_bounds()`는 경계, 중심, 반경을 계산합니다.
- `load_with_assimp()`는 유니코드 경로에 공통 바이너리 읽기를 사용한 뒤 Assimp를 호출합니다. aiMesh의 정점, 법선, UV, 인덱스를 합치고 재질과 파트를 구성합니다. 첫 번째 확산 텍스처의 경로를 해석하고 경계를 계산합니다.
- `ml_api_version()`은 v2를 반환합니다. `ml_load_model()`은 출력을 초기화하고 예외를 처리하며 `MlMeshData`를 채웁니다. `ml_free_model()`은 공개 해제 진입점이고 `ml_get_last_error()`는 스레드 로컬 진단을 반환합니다.

### 기본 창 상태, 불러오기, 편집

#### `src/main_window/kme.h`

- **수학과 해시**의 `KmeByteHash64`는 GUI 캐시와 디버깅에 결정적 바이트 해시를 제공합니다. `Matrix`는 ABI에서 복사한 GUI 소유의 2차원 double 버퍼입니다.
- **맵 모델**은 `TrackEvent`, `OwnTrackEditMarker`, `OtherTrack`, Station/SpeedLimit/Section 데이터와 `TableRow` 모음으로 `MapModel`을 구성합니다. GUI에 필요한 원본 등록부, 리소스 목록 메타데이터, 리비전, 기능과 일반·장면 행렬을 복사해 소유합니다.
- **2D 데이터**의 `View2D`는 이동, 축척, 회전을 보관합니다. `TrackPoint`, `PlanMarker`와 별칭, `PlanRepeaterSegment`, `OtherTrainPathOverlay`, `PlanData`, `ProfileData`는 캔버스 캐시와 히트 검사에 사용합니다.
- **표와 원본 도구**의 `TableRow/ColumnDef`, `CachedTableRow`, `TableUiCache`는 리비전에 따른 표시 데이터를 저장합니다. 파일 구조도, 텍스트 미리 보기, DistanceResolution 구조체는 레이아웃, 선택, 파서가 확인한 경계를 보존합니다.
- **설정과 런타임 상태**의 `TextureImage`는 D3D 배경 텍스처를 관리합니다. 로그, 창 표시 여부, 2D/3D 보기, `UserSettings`, 최근 맵, 배경 기록은 관련 상태와 INI 필드를 사용합니다. `ScenarioRoutePickState`는 후보 선택과 열기 옵션을, `ScenarioPreview`와 그 디스크 기준은 편집 가능한 시나리오 초안을 담습니다.
- **편집 상태 머신**의 필드 제약, 속성 창 세션, 대기 중인 변경, 미리 보기 스냅샷, 반복 구조물 초안, NewElement 템플릿과 마법사, 삭제 모드, 거리 작업, 편집 목록 초안은 미적용 UI 초안, 적용된 작업 사본, 저장된 디스크 상태를 구분합니다.
- **`App` 클래스**는 창 렌더링, 비동기 불러오기, 스냅샷 변환, 선형·장면 재구축, 표·탐색, 편집·저장·다시 불러오기, 대화 상자, 설정, 배경, 2D/3D 조작과 캐시를 선언합니다. 멤버 배치는 GUI `.cpp` 파일에서 공유하는 애플리케이션 상태를 정의합니다.

#### `src/main_window/maploader_runtime.cpp`

- `KME_MAPLOADER_FUNCTIONS`는 심볼 목록입니다. `MaploaderRuntime`은 `runtime_paths::dll_path()`에서 `maploader.dll`을 불러옵니다. 유일한 불러오기 진입점 `kv_load_map_ex()`을 포함한 모든 함수를 찾고 `kv_api_version()==KV_MAPLOADER_API_VERSION`을 검사합니다. 실패에 Win32 오류 텍스트를 포함합니다.
- 전역 `kv_*` 함수는 `ensure_loaded()`로 캐시된 DLL 포인터를 호출합니다. 오류 시 실패 값과 GUI에서 조회할 수 있는 진단을 제공합니다. GUI는 런타임에 DLL을 불러오고 공개 헤더의 함수 이름을 사용합니다.

#### `src/main_window/runtime_paths.h`와 `src/main_window/runtime_paths.cpp`

- `executable_directory()`는 `GetModuleFileNameW()`를 한 번 호출하고 EXE 폴더를 캐시합니다. `dll_directory()`는 그 아래의 `bin`을 선택하고 `settings_directory()`는 `settings`를 만들고 반환합니다.
- `dll_path()`는 DLL 경로를 구성합니다. `load_dll()`은 제한된 검색 플래그로 `bin`의 종속성을 불러오며 Win32 오류 코드를 반환할 수 있습니다.

#### `src/main_window/app_settings.h`와 `src/main_window/app_settings.cpp`

- 헤더는 기본 INI 경로, 명시 경로로 설정을 읽는 내부 함수, 설정·기록 I/O, 지연 ImGui 레이아웃 저장, 런타임 스타일 적용을 제공합니다.
- 앞부분의 도우미는 저장 경로, 최근 맵 키와 표시 이름을 정규화하고 글자, 조작 요소, 마커, 선 너비, 장면 거리, 기즈모, 인스턴스 경고 기준의 범위를 제한합니다. 색 도우미는 16진 직렬화, 팔레트, 알파, 혼합을 처리합니다.
- 언어, bool, 2D 모드, 격자 모드의 변환 함수가 INI 값 문법을 정의합니다. `save_user_settings()`는 `General`, `WindowVisibility`, `View2D`, `View3D`에 표준 키를 씁니다. `load_user_settings()`는 정확한 섹션, 키와 값 문법을 받으며 알 수 없거나 잘못된 항목에는 기본값을 사용합니다. 저장 작업은 기존 파일을 다시 씁니다.
- `load_imgui_layout()`, `save_imgui_layout()`, `save_imgui_layout_if_requested()`와 대기 플래그가 `imgui.ini`의 명시적·지연 저장을 관리합니다.
- `load_history_state()`는 `[Recent]`/`[MapN]`의 최근 맵과 경로·배경 필드 8개, `[CreatorMessages]`/`[CreatorMessageN]`의 제작자 메시지 설정을 읽습니다. 최근 경로를 정규화하고 중복을 제거하여 10개까지 유지합니다. `save_history_entries()`와 `save_history_state()`는 로캘에 독립적인 숫자로 표준 형식을 씁니다.
- `apply_ui_font_size()`, `apply_ui_theme_color()`, `apply_ui_component_size()`, `apply_ui_settings()`는 저장된 설정을 ImGui 스타일에 반영합니다. DPI와 뷰포트에 맞춰 모서리와 크기를 조정합니다.

#### `src/main_window/gui_kme.cpp`와 관련 모듈

- `gui_kme.cpp`는 `App` 생성·소멸과 로그 콜백을 관리합니다. `kme.h`는 EXE의 공유 상태와 번역 단위 간 인터페이스를 선언합니다.
- `win32_dx11_bootstrap.cpp`는 D3D11 장치, 렌더 타깃, `WndProc()`, 창 메시지 루프, `main()`을 담당합니다.
- `gui_common_utils.cpp`에는 글꼴, 테마·로그 색상, 인코딩 변환, 숫자 서식, 경로, 거리 이동 조작을 모읍니다. `background_image.cpp`는 WIC 디코딩, 배경 텍스처 재구축, 기록에 배경 설정 저장을 담당합니다.
- `map_snapshot_hydration.cpp`는 타입 스냅샷으로 `MapModel`, 행 메타데이터, 역 편집 ID, 제한 속도 캐시와 완화 구간 연결을 만들고, 불러오기와 저장에 필요한 시나리오 스냅샷을 복사합니다. `map_load_pipeline.cpp`는 맵·시나리오 판별, 시나리오 초안 기준, Route 후보, 비동기 맵 불러오기, 시작 문서 기록, 결과 적용, 메타데이터 병합, 불러오기 시간, 선형 재생성을 처리합니다.
- `edit_ledger.cpp`는 타입 배치와 보고서, 원장 동기화, 로컬 미리 보기, 삭제, 저장, 되돌리기, 닫기를 처리합니다. `distance_resolution_workflow.cpp`는 거리 배치 요청과 적용 재개를 담당합니다.
- `edit_benchmark.cpp`는 별도 Debug 편집 벤치마크를 제공합니다. 실제 입력 파일의 바이트를 보호하면서 메모리 적용·삭제·되돌리기를 수행합니다. 저장은 상대 경로 의존성을 유지한 전용 임시 폴더의 일반 파일 사본에서 실행합니다. 갱신 병합, 롤백, 단계 시간 계약도 검사합니다.
- `element_inspector_data.cpp`는 속성 창 열기와 위치 찾기, 필드와 장면 편집 데이터, 적용을 처리합니다. `element_inspector_render.cpp`는 속성 필드, 삽입의 선택 인수, 가변 길이 Repeater/Section 조작을 그립니다.
- `editable_list_drafts.cpp`는 리소스 목록 초안을 관리합니다. `new_element_wizard.cpp`는 새 요소 템플릿, 마법사, 구조화된 삽입, 새 파일 마법사 상태와 렌더링을 담당합니다. `headless_entrypoints.cpp`는 실제 App 절차를 재사용해 새 요소, 리소스 목록 교체·삽입, 새 파일 마법사, 시나리오 생성 계약을 검증합니다.
- `app_dialogs.cpp`는 파일 대화 상자, 시나리오 Route 후보 선택, 새 시나리오 파일 내용, 기타 모달을 담당합니다. 구조물 목록의 모델 파일 선택기는 3D 모델 미리 보기에서 임의의 모델을 여는 데도 사용합니다. `element_inspector_data.cpp`는 지연된 새 파일 요청, 배타적 생성, 결과 열기를 처리합니다. `ui_elements.cpp`는 도킹 공간, 메뉴, 도구 모음, 상태 표시줄, 콘솔, 단축키와 설정 반영을 담당합니다. `scene_preview_lifecycle.cpp`는 장면·모델 미리 보기의 시작, 종료, 재구축, 표시 여부와 창 렌더링을 관리합니다.

#### `src/main_window/file_structure_diagram.cpp`

- 레이아웃은 Include 깊이에 따라 노드를 묶고 크기, 연결, 경계, 리비전을 캐시합니다. `file_structure_layout_is_current()`가 캐시를 확인합니다. 원본 구조, 글꼴 또는 크기가 바뀌면 `rebuild_file_structure_layout()`이 재구축합니다.
- `open_parent_directory_in_explorer()`는 ShellExecute로 물리 폴더를 엽니다. `render_source_file_context_menu()`는 구조도와 속성 창에 공통 폴더 열기와 원본 미리 보기 작업을 제공합니다. 편집 모드의 Include 노드는 지연 파일 교체와 참조 해제도 요청할 수 있습니다.
- `App::render_file_structure_window()`는 이동 가능한 캔버스, 계층 연결선, 시작 파일과 Include 노드, 툴팁과 상황에 맞는 메뉴를 그립니다. 선택한 노드는 작업 사본의 텍스트 미리 보기로 넘깁니다.

#### `src/main_window/text_preview.cpp`

- `build_text_preview_lines()`는 각 줄의 시작 바이트 오프셋 인덱스를 만듭니다. `decode_preview_bytes()`는 파서가 확인하지 않은 파일에 읽기 전용 디코딩을 제공합니다. 일반 맵·목록은 `kv_get_source_text()`를 우선 사용하여 메모리 적용 후의 작업 사본을 표시합니다.
- 경계 범위·사이 구간·파일 끝 함수는 줄 번호로 `DistanceResolutionBoundary`를 찾습니다. 마커 스타일과 렌더링은 원본 줄 사이에서 파서가 확인한 삽입점을 표시합니다. `utf8_byte_for_source_column()`은 원본 열을 ImGui 텍스트 선택 바이트 오프셋으로 변환합니다.
- `open_text_preview()`, `refresh_text_preview_from_working_copy()`, `refresh_text_preview_after_map_load()`는 일반 미리 보기를 관리합니다. `open_text_preview_for_distance_resolution()`은 후보 경계와 대상 구문 위치를 제공합니다.
- `render_text_preview_window()`는 줄 번호, 읽기 전용 UTF-8 텍스트, 선택 영역, 원본 위치, 경계 버튼을 그립니다. 사용자가 보고서의 경계 토큰을 선택하면 기본 편집 상태 머신이 작업을 재시도합니다.

#### `src/main_window/touch_input.h`와 `src/main_window/touch_input.cpp`

- 헤더의 `TouchFrame`은 프레임마다 탭, 길게 누르기, 스크롤, 핀치 이벤트와 `PinchAxis`를 모읍니다. 공개 함수는 Win32 메시지, 프레임 상태, 영역별 이벤트 소비, 팝업 제스처와 테스트 주입을 처리합니다.
- `ActiveTouch`, `PairState`, `TouchManager`는 포인터 ID, 누른 위치와 시간, 이동 기준, 두 손가락 중심과 축척을 추적합니다. 메시지 처리는 누름, 갱신, 해제, 캡처 상실을 구분합니다. 길게 누르기와 스크롤은 동시에 발생하지 않으며, 핀치는 거리 변화를 축 배율로 바꿉니다.
- `new_frame()`은 일시 이벤트를 게시한 뒤 비웁니다. `consume_*`는 제스처를 사용했다고 표시하며 `apply_touch_scroll_to_hovered_window()`는 이를 ImGui 스크롤로 바꿉니다. `debug_*`는 제어하는 시계와 합성 터치 지점으로 상태 머신을 검증합니다.

#### `src/main_window/map_marker_visuals.cpp`

- `append_polyline/polygon/circle/glyph/sampled_arc()`는 정규화한 도형을 아이콘 정의에 추가합니다. 역, 곡선, 구배, 제한 속도, 지상자, 선행 열차, 소리·소음, 배경, 점착, 운전실 조도, 안개와 다른 궤도의 `*_recipe()`는 공통 아이콘 모양을 정의합니다.
- `map_marker_theme_color()`는 시각 유형의 테마 색을 반환합니다. `map_marker_role_color()`는 채움, 윤곽, 강조, 텍스트의 역할을 테마와 결합하고, `map_marker_icon_recipe()`는 유형과 변형에 맞는 정의를 선택합니다.
- `draw_map_marker_icon()`은 `transform_icon_point()`로 정규화 좌표를 2D 화면에 맞춰 확대·회전·이동한 뒤 ImDrawList로 기본 도형을 그립니다. 3D 코드는 같은 정의에서 빌보드 정점을 생성합니다.

### 2D 보기와 차트

#### `src/canvas2d/canvas2D.cpp`

- **조회와 보기 데이터**에서 `current_plan_data()`는 기준 궤도 표본, 역, 제한 속도와 마커를 선형과 표시 상태에 따라 캐시합니다. `fit_plan_view()`는 캐시된 범위에 표시 중인 다른 궤도와 반복 구조물의 끝점 및 블록 모서리를 합칩니다. 합친 범위의 긴 변 길이의 5%만큼 네 방향에 여백을 둡니다. 종단면 데이터도 생성과 조회를 나눕니다.
- **보기 조작과 렌더링**은 측정, 초점, 좌표 변환, `jump_to_distance()`에 공통 탐색을 사용합니다. `render_plan_canvas()`는 마우스·터치 입력, 표시 거리 계산, 배경, 격자, 궤도, 반복 구조물, 역, 마커, 레이블, 3D 위치와 초점 그리기를 조정합니다.

#### `src/canvas2d/canvas2d_view_state.h`와 `src/canvas2d/canvas2d_view_state.cpp`

- `App`이 소유하는 `View2D`는 화면 중심, 축척, 회전, 전체 맞춤과 드래그 상태를 저장합니다. 월드와 화면 사이 변환, 화면 이동량을 이용한 이동, 경계에 맞춤을 제공합니다.

#### `src/canvas2d/canvas2d_marker_cache.h`와 `src/canvas2d/canvas2d_marker_cache.cpp`

- 행렬 행·표본·하한·상한 함수는 거리에 따라 기준 및 다른 궤도의 행렬을 샘플링합니다. 로컬 오프셋 함수는 마커를 배치합니다. 반복 구조물의 LOD, 경계와 청크 구성이 연속 배경의 범위를 정의합니다.
- `rebuild_speed_limit_marker_overlay_cache()`와 `rebuild_marker_overlay_cache()`는 편집 ID, 원본 행, 궤도 위치에서 평면도 마커, 반복 구조물 구간, 타 열차 경로, 표시 인덱스를 생성합니다. 캐시와 무효화는 `App`이 소유합니다.

#### `src/canvas2d/canvas2d_interaction.h`와 `src/canvas2d/canvas2d_interaction.cpp`

- 측정 히트 검사는 평면도 세대와 축척으로 공간 격자를 캐시하고, 작은 데이터는 전수 검색합니다. 마커 히트 검사는 화면 반경, 행 순서, 겹침 우선순위로 대상을 고릅니다.
- 상황별 대상 수집, `render_plan_marker_context_menu()`, `plan_context_source_for()`는 마커 유형마다 표 탐색, 속성/편집, 삭제와 원본 위치를 제공합니다.

#### `src/canvas2d/canvas2d_background.h`와 `src/canvas2d/canvas2d_background.cpp`

- 배경 모듈은 월드·UV 변환과 화면 사각형 그리기를 처리합니다. 역 두 곳의 좌표와 이미지 두 점으로 축척, 회전, 평행 이동을 계산합니다. `App`은 배경 기록을 저장합니다.

#### `src/canvas2d/canvas2d_primitives.h`와 `src/canvas2d/canvas2d_primitives.cpp`

- `PlanScreenTransform`은 모델·평면도·화면 좌표를 변환합니다. 폴리라인 구성, 범위 클리핑, 반복 구조물 청크 및 개요 LOD는 보이는 선형을 선택합니다.
- 격자, 축척 막대, 삼각형, 마름모, 신호기, 선행 열차, 방향 화살표와 텍스트 함수는 저수준 ImDrawList 그리기를 감쌉니다.

#### `src/canvas2d/profile_plots.cpp`

- 도우미는 벡터 곡선, 제목·단위, 축 가장자리 마스크, 좌·우 곡선 반경 부호, 하단에 붙는 레이블, 종단면 수직 마커를 그립니다. RAII 클래스는 ImPlot의 전체 맞춤 버튼과 휠 확대 동작을 일시적으로 바꿉니다.
- 터치 확대는 `TouchFrame`을 X 또는 XY 축 범위로 바꿉니다. 데이터가 없거나 재구축될 때 `preserved_plot_span()`으로 적정 범위를 유지합니다.
- `render_profile_plot()`은 거리·표고, 구배 채움과 레이블, 역, 제한 속도, 편집 가능한 곡선·구배 마커를 그립니다. 공통 초점, 두 번 클릭 측정, 마우스 올리기와 상황별 작업도 처리합니다.
- `render_radius_plot()`은 거리와 곡선 반경을 그리고 좌·우 곡선을 구분하며 완화 구간 마커 연결을 재사용합니다. `render_plots()`는 현재 2D 모드에 창 공간을 배정하고 캐시한 `ProfileData`를 공유합니다.

### 3D 렌더링과 장면 구성

#### 3D 미리 보기 모듈

`Canvas3D::Impl`이 미리 보기 상태, 작업 스레드, GPU 리소스와 캐시를 소유합니다. 내부 헤더는 상태와 인터페이스를 선언하며 인라인 수학 함수와 Repeater 방문자 템플릿을 담습니다. CMake는 기능별 `.cpp`를 따로 컴파일합니다.

| 모듈 | 역할 |
| --- | --- |
| `canvas3D.cpp`, `canvas3d_impl.h` | 공개 인터페이스의 위임과 공통 내부 메서드·상태 선언 |
| `canvas3d_math.h`, `canvas3d_types.h` | 인라인 벡터·행렬 연산, CPU/GPU 레코드, 공통 상수와 조건 함수 |
| `canvas3d_scene_data.cpp/.h` | 타입 맵·장면 변환, 노선 값과 역, 마커 메타데이터, 안개 입력 변환과 표시 거리 |
| `scene_fog.cpp/.h`, `scene_shader_source.h` | CPU 안개 키프레임 구성·샘플링과 장면 HLSL. `route_value_sampling_contract`에서 CPU 로직 및 셰이더 컴파일 검증 |
| `canvas3d_scene_lifecycle.cpp` | 장면 교체, 동적·맵·역 데이터 갱신, 표시 여부와 설정, 모델 요청, 리소스 수명 |
| `canvas3d_model_loader.cpp/.h` | model-loader v2 클라이언트, WIC 텍스처와 캐시, CPU 모델 작업 스레드, 업로드 큐, 진단 |
| `canvas3d_put_between.cpp/.h` | 원본 모델 준비와 변형, 비동기 PutBetween 미리 보기, 순번 검사 후 결과 반영 |
| `canvas3d_model_preview.cpp` | 단일 모델 불러오기, 리소스 정리, 대화형 미리 보기. App 진입점은 구조물 목록 밖의 모델도 엶 |
| `canvas3d_d3d_resources.cpp` | HLSL, 셰이더 파이프라인, 깊이·블렌드·래스터라이저 상태, 렌더 타깃, 인스턴스 버퍼 |
| `canvas3d_scene_geometry.cpp/.h` | 장면·궤도 청크, 궤도 배치 프레임, 반복 구조물 인스턴스와 캐시 |
| `canvas3d_scene_markers.cpp` | 마커 정점, 텍스트와 아이콘, 글꼴 캐시, 표시 인덱스, 마커 그리기 |
| `canvas3d_scene_camera.cpp` | 궤도 샘플링, 카메라 이동과 점프, 초점 대상 상태 |
| `canvas3d_scene_edit.cpp`, `canvas3d_scene_gizmo.cpp` | 배치 미리 보기 갱신과 무효화, 기즈모 투영·히트 검사·끌기·그리기 |
| `canvas3d_scene_render.cpp`, `canvas3d_scene_ui.cpp` | 렌더 패스, 선택·강조, 보이는 인스턴스, ImGui 조정, 오버레이, 상황에 맞는 메뉴와 지연 작업 |
| `tests/scene_render_contract.cpp`, `tests/scene_loader_contract.cpp` | Debug 렌더링, 캐시, 픽셀, 선택 계약과 모델 로더의 소유권·실패 계약 |

#### 렌더링과 조작

- **궤도 샘플링**의 `scene_track_sampling.cpp/.h`는 일반 궤도 샘플링, 궤도 시작 전 카메라 외삽, 카메라 거리 경계를 제공합니다. 시작점 앞의 외삽은 카메라에만 적용합니다.
- **장면 데이터**에서는 벡터, 행렬, 경계 상자 함수로 월드 변환을 만듭니다. CPU/GPU 레코드는 모델, 재질, 텍스처, 청크 인스턴스, 마커, 선택 대상과 역방향 탐색을 관리합니다.
- **모델 불러오기**의 `ModelLoaderClient`는 `bin/model_loader.dll`에서 v2 API를 불러오고 할당과 해제를 짝지어 처리합니다. 작업 스레드는 CPU 모델 데이터를 복사하고 메인 스레드는 `upload_pending_scene_models()`로 D3D 리소스를 만듭니다. 수명 주기 모듈은 취소, 스레드 합류, 깨우기와 업로드를 조정합니다.
- **리소스와 장면 수명**에서 `load_model()`, `upload_model()`, `reload_model()`은 단일 모델 미리 보기를 관리합니다. `load_scene()`, 동적·맵·역 데이터 갱신과 `clear_scene()`은 장면 교체와 갱신을 관리합니다. 파이프라인은 필요할 때 만들고 텍스처 캐시를 재사용하며 인스턴스 버퍼는 필요에 따라 확장합니다. 리소스마다 대응하는 해제 함수를 사용합니다.
- **청크와 배치**의 `build_scene_chunks()`는 Structure, Signal, Repeater, 궤도 선형을 거리별로 구성합니다. 궤도 샘플링, 캔트 프레임, `make_track_placement_frame()`과 `make_track_world()`는 BVE 배치 매개변수를 월드 행렬로 바꿉니다. 장면은 카메라 상대 좌표와 역방향 Z 깊이를 사용합니다.
- **마커와 선택**은 공통 2D 아이콘 정의에서 3D 빌보드를 만듭니다. 표시가 바뀌면 마커 인덱스를 재구축합니다. 선택 패스는 객체·마커 ID를 쓰고 한 픽셀을 읽습니다. 강조 마스크와 외곽선 합성은 마우스를 올리거나 선택한 대상의 윤곽선을 그립니다.
- **노선 정보**의 `scene_route_overlay.cpp/.h`는 공통 노선 값 샘플링을 재사용해 반경, 캔트, 구배, 제한 속도, 폐색 구간 신호 속도와 다음 역을 표시합니다. `Curve.Interpolate` 구간은 양 끝의 계산한 반경·캔트, 방향 화살표, 삼각형 구분자를 표시합니다. 두 반경이 모두 0이면 현지화된 `직선` 레이블을 사용합니다.
- **프레임 조정**의 `canvas3d_scene_ui.cpp`에 있는 `render_scene_preview()`는 비동기 업로드, 카메라, 기즈모, 표시 인스턴스, 그리기, 선택, 강조, 상황별 메뉴를 처리합니다. 이후 탐색·편집·삭제 작업을 지연 요청으로 반환합니다. `canvas3d_scene_render.cpp` 등의 모듈이 렌더 패스를 수행합니다.

기즈모는 편집 대상별로 `Canvas3DPlacementDragUpdate`를 생성합니다. 일반 배치 좌표는 밀리미터 단위로 절삭합니다. Sound3D의 X/Y는 원본 오프셋, Z는 1 m 단위 거리를 바꿉니다. 명시적 Repeater End의 Z는 구간 끝을 바꿉니다. `Structure.PutBetween`의 Z축은 기준 궤도의 전방을 따르며 거리를 1 m 단위로 맞춥니다. 작업 스레드는 최신 대상만 처리하고 모델의 길이 방향 단면별 궤도 표본을 재사용해 결과를 재사용 가능한 동적 정점 버퍼에 반영합니다.

#### 프레임률과 단계별 시간 측정

표시 FPS는 장면 렌더 호출 빈도를 잽니다. `canvas3d_scene_ui.cpp`는 `0.1`초 이하의 양수 간격을 누적하고 활성 시간이 `0.2`초에 도달하면 `interval_count / active_seconds`를 게시합니다. 긴 간격은 완료하지 않은 구간을 폐기하고 마지막 측정값을 유지합니다. 초기화는 기준점, 누적값, 측정값을 지웁니다. 기본 루프의 Present와 렌더 깨우기는 `win32_dx11_bootstrap.cpp`가 담당합니다.

`scene_frame_profile.h`는 Debug 단계별 시간 측정을 제공합니다. 렌더링 및 로더 계약은 `NDEBUG` 가드로 EXE의 Debug 빌드에 포함되며 장면 벤치마크와 로더 headless 명령으로 실행합니다.

#### 안개

`scene_fog`는 반영한 `Fog`와 `Legacy.Fog` 행에서 키프레임을 만들고 장면 갱신 시 다시 구성합니다.

- 이벤트는 거리와 전체 원본 순서로 처리합니다. 거리가 0이 아닌 Legacy 구문은 원래 거리의 이전 상태 노드와 `distance + 25`의 목표 노드로 펼친 뒤 안정 정렬합니다.
- 같은 거리에서는 첫 노드가 이전 구간의 보간 목표이고 마지막 노드가 그 거리에서 적용됩니다. 생략한 Fog 매개변수는 앞서 넣은 노드 중 가장 먼 노드의 값을 상속합니다. 이후 거리의 Legacy 목표도 포함합니다.
- 이진 검색으로 샘플링합니다. 같은 모드 안에서는 밀도, RGB, 시작·끝을 double 정밀도로 보간하고, 모드가 다르면 이전 노드를 적용합니다. 이벤트가 없으면 안개는 꺼진 상태를 유지합니다. 처음 생략한 값은 기본값을 사용합니다.
- 입력 변환은 유효하지 않은 거리와 구간 값을 거르고 색을 0–1로 제한하며 셰이더 거리를 안전한 float 범위로 제한합니다.

장면 픽셀 셰이더는 카메라 공간 깊이(투영 `w`)를 이용해 지수 안개 또는 선형 안개 `clamp((end-depth)/(end-start), 0, 1)`를 계산합니다. 구간 너비가 0이면 `end`에서 계단식으로 바뀌고, 유한한 역순 구간은 같은 수식을 사용합니다. 안개는 배경, 모델과 궤도에 적용합니다. UI, 마커, 강조 마스크는 별도로 그립니다. 수식과 깊이 기준은 Microsoft의 [안개 수식](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas)과 [픽셀 안개 깊이](https://learn.microsoft.com/en-us/windows/win32/direct3d9/pixel-fog)를 참조하세요.

`src/canvas3d/tests/scene_fog_tests.cpp`는 `route_value_sampling_contract`에 포함됩니다. Legacy·혼합 모드, 인접 경계와 같은 거리 경계, 상속, 켜기·끄기, 수치 경계를 검사합니다. `D3DCompile`로 장면의 공통 정점 셰이더와 일반·안개 픽셀 셰이더 진입점을 컴파일합니다.

### 데이터 표와 보기 사이 탐색

다음 파일은 `src/table/`에 있으며 CMake에서 각각 컴파일합니다. `App`이 `TableUiCache`를 소유합니다. `datatable_cache.cpp`가 로컬에서 캐시를 만들어 이동으로 반영하고, `table_navigation.cpp`가 무효화 후 상태 초기화와 보기 사이 탐색을 처리합니다.

| 모듈 | 역할 |
| --- | --- |
| `datatable.cpp` | 공통 셀·수치 변환, 표 UI 기본 도우미, 장면 궤도 키의 의미 주석 |
| `datatable_internal.h` | 내부 인라인 열 정의와 작업·뷰·도우미 선언. 창, 캐시 구성, 전용 템플릿은 각각의 `.cpp`에 위치 |
| `datatable_cache.cpp` | 전체 `TableUiCache` 반영, 동적 Section/Signal 열, 병합한 Repeater 표시 행, 열 너비 측정, 제한 속도 런타임 캐시 갱신 |
| `datatable_find.cpp` | 대소문자를 구분하지 않는 찾기·초기화·이동, 미사용 키 검색, Structure/Signal/Sound 전용 App 검색 API |
| `datatable_resource_lists.cpp` | 편집 목록 공통 렌더링, 역·구조물 모델·신호 현시·Sound·Sound3D 리소스 목록 창 |
| `datatable_route_tables.cpp` | 다른 궤도, Station.Put, Structure, 타 열차, Repeater, Signal.Put, Section, 변수 창 |
| `datatable_effect_tables.cpp` | Beacon, Irregularity, 소리·소음, Background, Adhesion, CabIlluminance, Fog, Lighting, DrawDistance, SpeedLimit 창 |
| `datatable_scenario.cpp` | 시나리오 파일 표 및 경로·후보 편집 UI |
| `datatable_benchmark.cpp` | 실제 맵의 캐시 재구축·예열 후 적중·입력 없는 표 프레임을 측정하는 Debug 벤치마크, 캐시 요약 및 원본 무결성 검사 |
| `table_navigation.cpp` | 캐시 무효화 후 상태 초기화, 표·평면도·장면 탐색 |

맵 표는 `TableUiCache`와 표시 상태를 읽고 초안 변경, 선택, 탐색에 공통 `App` 메서드를 호출합니다. 시나리오 표는 별도의 시나리오 초안을 사용합니다.

#### `src/table/table_navigation.cpp`

- `invalidate_table_cache()`는 리비전과 파생 행을 지웁니다. `reset_marker_visibility()`와 `sync_marker_visibility_sizes()`는 2D 마커 플래그를 모델 행 개수와 맞춥니다.
- Structure, Repeater, Signal의 `locate_*_on_plan/in_list/in_scene_preview()`는 평면도 초점, 표의 강조와 창 표시, Canvas3D의 위치 이동을 갱신합니다. 반복 구조물은 종료와 변경 경계로도 이동할 수 있습니다.
- `standard_marker_navigation()`은 표준 마커 종류를 평면도 캐시, 표시 설정, 표 창, 초점, 스크롤, 강조 상태와 연결합니다. `None`과 `Count` 사이의 종류마다 항목이 하나인지 컴파일할 때 검사합니다. 평면도·표·장면에서 이 대응을 함께 사용합니다.
- 타 열차 정차 지점으로 이동하면 해당 그룹과 정차 행을 엽니다. 표와 반복 구조물 속성은 `can_locate_scene_preview_row()`로 장면 준비 상태를 확인합니다.

### Debug 진입점과 계약 테스트

#### `src/main_window/debug_headless.h`

- 각 `*Options` 구조체는 하나의 명령줄 모드에 대응합니다. 맵·시나리오 불러오기, 평면도·장면·열기·편집·표 캐시 벤치마크, 장면 로더·카메라 전환, 진단 팝업, 원본 기준점, 왕복, 거리·기준 궤도·다른 궤도, 역·리소스 목록, 반복 구조물, 폐색 구간, Include, 새 파일·요소, 표 검색, 터치, 설정 저장을 다룹니다.
- 헤더는 Debug 전용 `run_debug_headless_*()` 진입점을 선언합니다. 옵션 구조체는 `main()`의 명령줄 파싱과 테스트 구현을 연결합니다.

#### `src/main_window/debug_headless.cpp`

- **공통 유틸리티와 옵션**은 COM RAII, UTF 경로, 출력 파일, 시간 통계, 해시, 스냅샷 행렬 요약, 로그 캡처, 픽스처 찾기로 결정적 headless 출력을 제공합니다. 이 파일에서 모드별 옵션도 파싱합니다. 편집 벤치마크는 별도의 `edit_benchmark.cpp`에, 표 캐시와 그리기 벤치마크는 `src/table/datatable_benchmark.cpp`에 구현합니다.
- **불러오기, 선형, 장면 검사**에서 기본 맵 불러오기는 스냅샷 구조와 행렬을 검증합니다. 시나리오 불러오기는 v2 스냅샷, 편집 왕복, 후보 선택, 해석한 맵을 검증합니다. 평면도·장면 벤치마크는 캐시를 반복 구축하고 단계 시간, 개수, 해시를 보고합니다. 카메라 전환은 재구축 전후 자세를 검사합니다. 장면 디버깅은 픽셀과 안개 상태를 읽어 렌더링을 검증합니다.
- **`typed_edit_headless`**의 `Field/Change/Batch/Report`는 공개 편집 ABI의 RAII 래퍼입니다. 문자열 뷰의 수명, 모의 실행·적용·커밋 보고서 복사와 실패 정보를 관리합니다.
- **거리·기준 궤도·다른 궤도 배치**의 `distance_batch_headless`는 핸들, 편집 대상, 경계 선택, 보고서로 여러 파일·Include·변수 사례를 실행합니다. 기준·다른 궤도 모드는 메서드와 인수 형식 보존, 적용·초기화·커밋, 선형 변경을 검사합니다.
- **목록과 연동 편집**의 `station_list_edit_headless`는 임시 CSV를 만들어 편집, 비우기, 재정렬, 삭제와 원래 인코딩을 검사합니다. `repeater_batch_headless`는 연결 구문 갱신, 잘라내기 변환, 원자적 삭제를 검사합니다. `section_edit_batch_headless`는 동적 인수 추가·삭제, null·식 보존과 커밋을 검사합니다.
- **삽입과 원본 기준점** 모드는 허용 템플릿, 거리 블록 선택, 알 수 없는 필드의 거부를 검사합니다. 원본 기준점·왕복 모드는 물리 파일, Include 스택, 줄·열·범위, 안정적 ID와 저장 후 다시 읽은 결과의 일치를 검사합니다.
- **UI와 영속성 검사** 중 표 찾기는 대소문자, 완전 일치, 결과 이동과 미사용 상태를 확인합니다. 터치는 합성 입력으로 탭, 길게 누르기, 스크롤, 핀치, 이벤트 소비를 검사합니다. 설정 저장은 임시 디렉터리에서 표준 형식, 잘못된 항목의 기본값, 읽은 뒤의 파일 바이트, 기록 경계를 검사합니다. 각 진입점은 PASS/FAIL을 보고합니다.

#### `src/main_window/edit_benchmark.cpp`

- `App::run_debug_headless_edit_benchmark()`는 실제 App의 속성 창 적용, 지연 삭제, 저장, 되돌리기, 갱신과 첫 장면 프레임 경로를 사용합니다. 작업별 포함 시간과 집계 통계를 보고합니다. 입력 노선과 불러온 모든 물리 원본 파일의 바이트를 보호합니다. 저장은 배타적 임시 루트 아래 일반 파일 사본에서 수행합니다. 복사할 때 Windows 재분석 지점과 임시 루트 밖의 대상을 거부합니다.
- 갱신 픽스처는 전체·부분 데이터 반영의 병합, 표·평면도 캐시의 일회 무효화, 적용 실패 후 복구, 빈 저장을 검사합니다. `NDEBUG` 가드로 Debug에서만 구현을 포함합니다.

#### `src/main_window/headless_entrypoints.cpp`

- 실제 `App` 편집 상태와 대화 상자 요청 처리를 재사용합니다. 새 요소 적용·속성 창·삭제, 리소스 목록 교체와 행 삽입, 맵·시나리오와 다섯 리소스 목록 유형의 생성 또는 재사용, 참조 커밋, 다시 불러오기, 정리, 시나리오 수명 주기를 검증합니다.
- 새 파일 검증은 `tests/` 아래에 아직 없는 경로를 요구하며 생성한 파일은 검증 후 제거합니다. 리소스 목록 삽입과 교체는 메모리 적용을 검증합니다. 교체에는 실제 파일 선택기를 사용합니다.

#### `src/maploader/tests/typed_snapshot_tests.cpp`

- `TempFixture`는 임시 맵과 목록 파일을 만들고 지웁니다. 인코딩 도우미는 UTF-8/BOM, UTF-16, CP932 입력을 생성합니다. `MapHandle`은 RAII로 `kv_free()`를 호출하며, `CHECK_ARRAY` 같은 검사는 개수와 null 포인터 계약을 함께 확인합니다.
- 스냅샷 테스트는 모든 루트 배열, 문자열·범위, 메타데이터, 기능, 리비전, 안정적 편집 ID를 순회합니다. Windows 내보내기 테이블에서 `kv_load_map_ex()`가 유일한 불러오기 진입점인지 확인합니다. Signal 눈부심·가변 키, 리소스 Load, Include, 장면 스냅샷도 집중 검사합니다.
- 선형 테스트는 구배와 곡선 픽스처로 노선 길이, 평면 투영, 표고, 이벤트 거리를 비교합니다.
- `UpdateBatch`와 `RepeaterTrimBatch` 같은 래퍼는 타입 편집을 구성합니다. 편집 테스트는 모의 실행, 메모리 적용·초기화, 직접 적용, 커밋, 동시 수정 해시, 거리 배치, 메서드·인수 형식, 의미 보호, 인코딩, 트랜잭션 롤백을 검증합니다.
- 진단 테스트는 로컬 `tests/` 픽스처를 읽어 누락 파일, 잘못된 구문, 중복 Load/Enable, 짝이 없는 완화 구간, 알 수 없는 키, 로그와 마지막 오류 텍스트를 검사합니다. `main()`은 `snapshot`, `geometry`, `edit`, `diagnostics` 및 개별 옵션으로 그룹을 선택한 뒤 CTest에 프로세스 상태를 반환합니다.

#### `src/main_window/tests/route_value_sampling_tests.cpp`

- CPU 계약은 보간 끝점의 소유 관계, 생략 값 상속, BeginTransition, 유효하지 않은 수치, 3D 노선 정보의 양수·음수·0 반경 서식, 양 끝이 0인 보간의 직선 레이블을 검사합니다.

### 정적 호출 흐름

```text
main / App
  -> maploader_runtime의 kv_* 전달 함수
     -> maploader.cpp의 C ABI 예외 경계
        -> scenario_route -> KvScenarioSnapshot / Route 후보
        -> parse_map_context -> Parser -> MapContext
        -> generate_geometry -> Matrix / 장면 제어점
        -> MapSnapshotBuilder -> KvMapSnapshot
        -> build_edit_report -> 패치 적용 원본 재파싱과 의미 검증 -> 메모리 오버라이드 또는 트랜잭션 쓰기

App / MapModel
  -> datatable, canvas2D, profile_plots에서 리비전별로 캐시되는 2D 보기 생성
  -> Canvas3DScene -> Canvas3D::Impl -> D3D11 청크, 비동기 모델, 선택, 기즈모
  -> 속성 창 / 인라인 초안 -> KvEditBatch -> maploader의 원본 우선 편집
```

`MapContext`가 파서와 스냅샷 저장소를 소유합니다. GUI는 필요한 데이터를 `MapModel`에 복사합니다. 캔버스는 리비전별 파생 캐시를 관리합니다. DLL에서 반환한 독립 문자열과 모델 배열은 해당 DLL의 대응 해제 함수로 해제합니다.

## 핵심 개발 규칙

### C++와 ABI

- C++17을 사용하고 RAII, 표준 컨테이너, `std::filesystem`, 책임이 분명한 도우미를 우선합니다.
- `UNICODE`, `_UNICODE`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN` 정의를 유지합니다.
- 공개 C ABI는 고정 폭 POD 타입과 명시적 메모리 소유권을 사용합니다. 경계에서 예외를 처리하고 DLL에서 할당한 메모리는 짝이 되는 함수로 해제합니다.
- EXE는 maploader API v13, 맵 스냅샷 v9, model-loader API v2와 정확히 일치해야 합니다. `kv_load_map_ex()`가 유일한 맵 불러오기 진입점입니다. `KvScenarioSnapshot`과 `KvScenarioEditDocument`는 v2를 사용합니다. 맵, 장면 선형, 편집 대상, 보고서 계약은 각각 버전과 구조체 크기를 관리합니다.
- ABI 입력은 호출 동안만 유효한 뷰입니다. 중첩 스냅샷은 핸들이 소유하며 공개 헤더에 정의된 선형 재구축, 편집, 초기화, 재파싱, 해제 시 무효화됩니다. 시나리오 스냅샷은 별도로 할당하고 `kv_free_scenario_snapshot()`으로 해제합니다.
- 공개 ABI를 바꿀 때는 버전과 구조체 크기 정책을 명시하고 EXE·DLL·호출자를 함께 갱신하며 소유권과 수명을 문서화합니다.

### 파싱, 선형, 원본 보존

BVE Map 2.0 이상, 기존 구형 구문, Include, 변수, 미리 정의된 `distance`, 수학 함수, 주석, UTF-8/BOM, UTF-16LE/BE, CP932/Shift_JIS 입력을 지원합니다. 파싱과 프리셋에는 공식의 일반 BVE 구문을 사용합니다.

각 맵 문맥은 거리 0과 로컬 거리 식으로 시작합니다. Include는 일반 변수와 원본 식별 정보를 상속합니다. 병합은 자식의 이벤트, 제어점과 변수 쓰기를 반영하면서 상위 파일의 거리와 식을 보존합니다. 병렬 사전 파싱 결과는 변수 의존성과 대조하고 필요할 때 다시 파싱합니다.

편집 가능한 행은 물리 경로, Include 스택, 원본 범위, 원래 구문과 인수, 계산한 값, 거리 식, 파싱 순서, 안정적 ID를 보존합니다. `KvMapSnapshot`이 이를 강타입 뷰로 제공합니다. 쓰기는 원래 인코딩, BOM, 줄바꿈을 보존하고 원래 인코딩으로 표현할 수 없는 새 문자를 차단합니다.

AI 코딩 도구로 BVE 맵·목록·시나리오의 읽기, 검증, 타입 표현, 편집, 생성, 쓰기를 바꿀 때는 담당 스킬과 [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md)를 사용합니다. 구현 전 스킬에 따라 날짜가 기록된 공식 페이지 캐시를 확인하고 관련 페이지를 읽고 준수 표를 작성합니다.

#### 시나리오

`scenario_route.cpp/.h`는 다음 작업을 관리합니다.

- **읽기와 미리 보기**에서 파일 종류를 판별하고 `BveTs Scenario 2.00` 헤더를 검증하고 선언된 인코딩으로 읽습니다. `#`/`;` 주석을 처리하며 공식 필드 여덟 개마다 마지막 값을 보존합니다. 상대 경로, 가중치, 원본 해시와 필드 존재 비트도 보존합니다. Route 필드나 대상 파일이 빠진 시나리오도 스냅샷으로 미리 볼 수 있습니다.
- **맵 열기**에서 `kv_resolve_scenario_routes()`는 Route 후보와 대상 파일을 확인해 검증할 맵을 맵 로더로 전달합니다. Vehicle 데이터는 미리 보기에 사용합니다. Vehicle 값이 완전히 비어 있으면 경고를 표시하고 유효한 Route 맵을 불러옵니다. Route 해석이나 맵 불러오기에 실패해도 시나리오 미리 보기를 유지합니다.
- **초안 저장**에서 GUI는 `저장`으로 시나리오를 직접 커밋합니다. 기존 Route에 후보를 하나 이상 유지합니다. Vehicle은 첫 후보 입력·파일 선택, 빠진 필드 생성, 마지막 후보 제거를 지원합니다. 빈 자리표시자는 초안을 변경하지 않습니다. 후보 구조 변경은 표 렌더링 뒤에 처리합니다. 값이 있는 후보는 초안 순서를 유지하고 예약 구문 문자가 없는 경로와 유한한 양수 가중치를 사용합니다. 후보 개수가 같으면 항목별로 패치하고 개수가 바뀌면 해당 필드의 후보 값을 다시 씁니다. 후보가 0개면 Vehicle 필드가 없는 상태를 유지하거나 기존 값을 비웁니다. 원본 해시를 검사하고 전체를 재파싱한 뒤 인코딩, BOM, 줄바꿈을 보존하며 트랜잭션으로 씁니다.
- **파일 생성**에서 새 파일 마법사는 `build_new_scenario_file_content()`로 공식 키 순서의 UTF-8/CRLF를 생성하고, 파일을 배타적으로 만든 뒤 검증을 위해 다시 파싱합니다. Route/Vehicle은 가중치가 없는 경로 하나로 시작합니다. 여러 후보와 가중치는 시나리오 파일 탭에서 편집합니다.

#### 곡선 매개변수와 조명

`Curve.SetGauge(value)`, `Curve.SetCenter(x)`, `Curve.SetFunction(id)`와 구형 `Curve.Gauge(value)`는 `CurveEditRow` → `KvCurveRow` → `MapModel::curve_rows`를 공유하며 기준 궤도 선형 상태 이벤트를 생성합니다. 갱신은 원래 메서드와 원본 식별 정보를 유지합니다. 세 생성 템플릿은 현재 메서드를 사용하며 기본값은 각각 `1.067`, `0`, `0`입니다. `SetFunction` 불러오기, 수정, 삽입에는 계산값이 `0` 또는 `1`인 수치 인수 하나가 필요합니다.

`Light.Ambient`, `Light.Diffuse`, `Light.Direction`은 `light.ambient`, `light.diffuse`, `light.direction` 대상과 `KvLightColorRow`/`KvLightDirectionRow`를 통해 표의 매개변수 미리 보기와 원본 편집을 지원합니다. 세 형식은 항상 표시되며 편집 조건을 만족하면 매개변수를 바꿀 수 있습니다. 적용, 삭제, 새로 만들기의 사용 여부는 대상과 초안 상태에 따릅니다. 적용은 바뀐 형식을 묶고 삭제는 지연 요청을 사용합니다. 마법사는 선택한 원본 파일의 거리 `0`에 구문을 만듭니다.

루트 맵과 Include를 병합한 뒤 조명 종류마다 구문이 유효한 선언을 하나만 유지할 수 있습니다. 중복되면 해당 종류의 충돌 행을 모두 무효화하고 모든 물리 위치를 영어 경고로 표시합니다. Ambient/Diffuse RGB 값은 `[0, 1]` 범위이며 Direction의 거리는 `0`이어야 합니다. 유효한 행은 스냅샷에 포함합니다. 수정은 바꾸지 않은 인수 식을 보존하고 전체 의미 검증을 거칩니다.

### 편집 모델

maploader가 원본과 편집 식별 정보를 소유합니다. GUI는 타입 요청으로 작업 사본을 편집합니다. Preview/Edit는 기능 비트에 맞춰 데이터를 반영합니다. 저장 전에 목록 표의 미적용 초안을 해당 표에서 적용합니다.

| 작업 | 역할 |
| --- | --- |
| `kv_edit_dry_run_typed()` | 패치 보고서를 생성하고 검증 |
| `kv_edit_apply_to_memory_typed()` / GUI 적용 | 메모리 작업 사본과 미리 보기 갱신 |
| `kv_edit_apply_typed()` | 트랜잭션으로 직접 쓰기 |
| `kv_edit_commit_typed()` / GUI 저장 | 검증된 작업 사본 커밋 |
| `kv_edit_reset_memory()` / GUI 되돌리기 | 메모리 오버라이드를 폐기하고 디스크 기준 복원 |
| GUI 다시 불러오기 | 저장하지 않은 변경을 확인한 뒤 디스크에서 다시 읽기 |

`sourceHash`는 작업 사본을 식별합니다. `expectedSourceHash`는 적용과 삭제를 반복하는 동안 디스크 기준을 식별합니다. 계획 단계에서 메모리에 변경 사항이 있는 파일을 포함해 편집 대상 물리 파일마다 디스크 바이트를 한 번 확인합니다. 미리보기와 편집 메타데이터의 물리 원본 목록 및 바이트 해시가 일치하면 편집기가 둘을 병합합니다. 적용과 저장은 후보 원본 전체를 다시 파싱하여 대상 값, 다른 요소, 최종 변수 바인딩을 확인합니다. 유효한 편집은 최종 `distance`를 바꿀 수 있습니다.

#### 거리 배치 계획과 원본 패치

거리 이동과 삽입은 파서의 경계 계획기를 공유합니다. 물리 파일, Include 호출, 거리 구간과 대상 거리로 묶어 처리하며 구문 순서, 주석, 빈 거리 블록을 보존합니다. 암시적 첫 블록, 기준점 사이, 마지막 블록, 파일 끝에서 배치할 수 있습니다. 경계가 분명한 마지막 구간은 증가 또는 감소 방향으로 늘어날 수 있습니다. 이동은 그 구간에서 시작해야 하며 삽입은 기존의 유일한 위치를 우선합니다. 후보 나열과 토큰 조회는 방향이 바뀌는 구간 뒤의 인접 범위를 포함해 같은 계획기를 사용합니다.

각 파일과 Include 인스턴스의 진입·종료 환경은 뒤에 있는 대입과 변수 쓰기를 기록하며 편집 메타데이터와 함께 재구축합니다. 환경 검사는 복구 가능한 문제에 `evaluation_Environment_Requires_Boundary` 후보를 만들고 배치할 수 없는 편집은 차단합니다. 보고서에는 `ambiguous_Source_Section` 등의 이유가 포함됩니다. 후보를 거른 뒤 선택한 배치의 맵 전체 의미를 검증합니다.

GUI의 최초 처리와 캐시 재사용은 작업 선택을 공유하며 진행을 막는 오류를 우선합니다. 실패한 재시도는 작업 사본 해시, 구조화한 변경, 배치의 모든 수동 선택으로 식별합니다. 거리 대입은 유한해야 하며 유한한 음수 거리도 허용합니다. 바꾸지 않은 객체 키 식, 주석, 줄바꿈은 원래 바이트를 유지합니다. Section의 `values.N`은 기존 인수를 참조해야 합니다. 길이를 바꿀 때는 `values.count`를 명시합니다.

#### 요소와 리소스 목록

- **메서드 변환**에서 일반 편집은 메서드와 인수 형식을 보존합니다. 속성 창의 좌표 오프셋 버튼으로 `Put`/`Put0` 및 `Begin`/`Begin0`을 명시적으로 바꿉니다. 0이 아닌 오프셋 제거, 짧은 `Signal.Put` 변환, 반복 구조물 잘라내기는 각각 확인을 거칩니다.
- **Legacy.Fog**의 `legacyFog.change`는 `distance/start/end/red/green/blue` 필드로 수정·삭제·삽입을 지원합니다. 다섯 구문 인수는 모두 유한한 수치여야 합니다. 음수 또는 같은 값, 역순 구간, 명목 범위 밖의 유한한 RGB도 허용합니다. 바꾸지 않은 식은 보존합니다. 기준과 편집은 의미 작성기를 공유합니다. Include 교체는 하위 트리 제외 규칙으로 대상 외 값을 보호하며, 갱신은 표, 장면 안개, 마커에 반영됩니다.
- **리소스 목록 행**의 Station, Structure, Signal, Sound, Sound3D는 인라인 초안을 공유합니다. 상황에 맞는 메뉴로 위아래에 행을 삽입합니다. Structure는 2개, Sound/Sound3D는 3개, Station은 13개 CSV 필드를 씁니다. Signal 기본 행은 6개 필드로 시작하며 이후 조정한 너비를 유지합니다. 기본·눈부심 행 쌍은 한 묶음으로 삽입하며 사용자가 눈부심 행을 명시적으로 추가합니다.
- **Signal 열**은 물리 줄의 오른쪽 끝 빈 필드를 보존합니다. `KvSignalAspectRow::metadata.reserved`는 기본 행의 구조물 필드 수를 저장합니다. 나머지 `structure_keys`는 눈부심 행에 속합니다. 형태 변경은 `mainStructureKeyCount`, `glareStructureKeyCount`와 번호가 붙은 모든 필드를 제공합니다. 셀 편집은 기존 형태를 유지합니다. 각 물리 행은 비워 둘 수 있는 구조물 필드 하나 이상을 유지합니다. 기본·눈부심 행의 경계는 의미와 변경 상태 검사의 일부입니다. 열 작업은 표시 한도인 509열을 넘는 실제 열도 포함합니다.
- **반복 구조물의 연결과 이름 변경**은 모든 계층이 `repeater_linkage`와 완화 구간 연결을 공유합니다. 수명은 반개구간 `[first Begin, End)`이며 거리, 전체 순서, 원본 행으로 정렬합니다. 같은 거리에서 마지막 이벤트가 활성 상태를 정합니다. 빈 구간도 원본 식별 정보를 유지합니다. 이름 변경 배치는 연결의 모든 Begin/Begin0과 명시적 End를 포함하고 이름 겹침과 연결 소유권을 검사합니다. 재파싱은 대상 외 경계를 검증하며 새 길이 0 구간은 Begin 다음에 End를 생성합니다.
- **반복 구조물 키**에서 스냅샷 반영, 속성 창, 장면은 `repeater_structure_keys()`와 `set_repeater_structure_keys()`로 `_structureKeys.count` 및 `_structureKeys.N`을 공유합니다. 연결한 `structureKeys` 텍스트는 표시용입니다. 모델 배열은 빠진 항목의 위치를 유지하고 `k % N`에 원래 목록 길이를 사용합니다. 끝점 모델이 없으면 카메라 이동은 배치 선형의 기준점을 사용합니다.
- **다른 궤도 이름 변경**의 타입 배치 하나에는 루트 맵과 모든 Include의 같은 키 `Track[trackKey].*` 구문이 전부 들어갑니다. 키 비교는 수치와 문자열 타입을 유지하며 대소문자를 구분하지 않습니다. 배치가 완전하고 새 키가 맵 전체에서 고유하면 이름 변경을 허용합니다. 키를 참조하는 다른 요소에는 대상 외 행 검증을 적용합니다.

### UI, 표와 렌더링

- Dear ImGui 도킹 레이아웃, 메뉴, 도구 구조를 유지합니다. 일반 UI 텍스트는 여섯 언어를 함께 갱신하며 언어를 바꿔도 ImGui ID를 유지합니다.
- BVE 매개변수 레이블은 `distance`, `trackKey`, `x`, `ry` 등 공식 영어 이름이나 약어를 사용합니다. 프로그램 진단과 headless 출력은 영어를 쓰며 콘솔 UI는 여섯 언어를 지원합니다.
- 2D 이동·확대·회전·전체 맞춤, 측정, 격자, 역 이동, 배경 정렬과 3D 카메라 전환, 선택·강조, 표시, 마커, 노선 정보, 연동 기즈모 동작을 유지합니다.
- 표를 리비전별로 캐시하고 Section의 동적 인수와 명시적 `null`, 변수 순서, 보기 사이 탐색을 유지합니다. 반복 구조물 검색에는 순서가 있는 타입 구조물 키를 사용합니다. 미사용 구조물을 찾기 전에 활성 Signal 기본·눈부심 셀을 초안에 반영합니다.
- Assimp는 `model_loader.dll` 안에서 사용합니다. 불러오기 오류는 진단과 정리 경로로 처리합니다. 모델 경계는 double 정밀도로 계산합니다. 유한하지 않은 위치와 공개 float 범위 밖의 반경은 거부합니다. 동적 장면 갱신에 실패하면 반복 구조물 청크와 캐시 합계를 복원합니다.

#### 초안과 보기 상태

`App`은 기존 행을 수정하거나 삭제할 때 원래 디스크 행을 보존합니다. 전체·부분 반영에서 이 기준을 사용합니다. 적용에 실패하면 이전 상태를 복원합니다. 성공하면 원장에서 빠진 항목의 기준 데이터를 제거하고, 저장·되돌리기는 완료된 항목을 지웁니다. 속성 창은 필드마다 변경을 추적하며 원래 값으로 돌아온 필드의 변경만 제거합니다. 다른 궤도의 표시, 색상, 표시 범위는 정확한 키로 유지합니다. 명시적으로 이름을 바꾸면 안정적 ID로 상태를 전달합니다.

#### 설정과 제작자 메시지

애플리케이션 설정, 최근 맵·배경과 제작자 메시지 설정, ImGui 레이아웃은 각각 `settings/settings.ini`, `settings/history.ini`, `settings/imgui.ini`에 저장합니다. 로더는 저장기가 쓰는 정확한 섹션, 키, 값 문법을 받습니다. 알 수 없거나 잘못된 항목에는 기본값을 사용하며 명시적 저장에서 파일을 다시 씁니다. 설정과 레이아웃은 쓰기와 닫기가 모두 성공한 뒤 저장 완료로 표시합니다. 실패 시 `App::service_pending_persistence`는 창이 가려지거나 대기 중일 때도 각각의 1초 기한에 따라 재시도합니다. ImGui 요청 플래그가 레이아웃의 변경 상태를 관리합니다.

제작자 메시지는 독립적인 `//--kme--message-from-creator:` 주석에서 읽고 물리 위치와 Include 식별 정보를 보존합니다. 같은 물리 메시지의 중복을 제거하고 `KvCreatorMessageRow`로 Preview/Edit에 전달합니다. `creator.message` 편집은 원래 `content`를 받으며, 헤더 삽입은 공통 패치·재파싱·커밋 경로를 따릅니다. `creator_messages.cpp`는 초안, 표, 문서 열기 팝업을 관리합니다. 기록의 `[CreatorMessages] count`와 `[CreatorMessageN] path/has_messages/suppressed`로 시작 맵별 표시 설정을 보존합니다. 메시지가 일시적으로 비어 있어도 숨김 설정을 유지합니다.

#### 곡선 마커

`CurveGauge`, `CurveCenter`, `CurveFunction` 마커는 행 인덱스와 편집 ID를 담습니다. 2D는 흰색 `CG`/`CC`/`CF` 사각형을 그립니다. 3D는 코드와 계산한 매개변수의 흰색 두 줄 표지를 그리며 선택과 파란색 강조를 공유합니다. `[View2D]`의 `show_curve_gauge_markers`, `show_curve_center_markers`, `show_curve_function_markers`는 기본값이 꺼짐이며 각 마커의 표시를 제어합니다.

`Curve.Interpolate`는 인수 0/1/2개 형태와 편집 식별 정보를 보존합니다. 데이터 반영은 원본 파일 인덱스와 전체 구문 순서로 계산된 이벤트를 연결합니다. 정렬한 이벤트와 같은 거리의 반복 Include도 처리합니다. 편집 메타데이터 병합 뒤 2D 끝점과 `CurveCircularStart` 스타일의 3D 표지는 같은 원본 행을 사용합니다. 속성/편집과 지연 삭제를 제공하며 구문마다 마커 하나를 표시합니다. 표지에는 계산한 반경·캔트가 표시되며 반경이 0이면 `Intpl. 0`을 사용합니다.

미리 보기에서는 평면도 마우스 올리기에 마커 배열 인덱스를 사용합니다. 원본 선택과 편집은 원본 연결이 완료된 뒤 활성화합니다. 3D는 `curve` 범주와 상황에 맞는 메뉴를 유지하고 메타데이터가 준비되면 편집·삭제를 켭니다. 생성 템플릿은 전체 `Curve.Interpolate(radius, cant);` 시그니처를 표시하며 뒤쪽 선택 인수를 사용해 세 형식을 생성합니다.

### 성능

- 읽기, 디코딩, 해시, 경로 해석, 스냅샷, 표, 마커, 선형을 입력과 리비전에 따라 재사용합니다.
- 큰 배열은 가능한 한 연속으로 두고, 내부 루프와 프레임마다의 할당을 제한하며, 큰 궤도 배열의 O(n²) 순회를 피합니다.
- 오래 걸리는 파싱과 모델 불러오기는 진행 상황을 제공하는 비동기 작업을 사용합니다. 캐시마다 완전한 키, 무효화 담당과 리비전을 정하고 적중과 무효화를 검증합니다.

`refresh_local_preview_after_edits()`는 적용과 롤백의 갱신을 병합합니다. 전체 반영에는 궤도·목록 부분 변환이 포함되고 전체 장면 재구축에는 이전 장면의 갱신이 포함됩니다. 단일 인스턴스와 반복 구조물 좌표 편집은 안정적 ID의 빠른 경로를 사용합니다. 거리와 물리 기준점 인덱스는 처음 사용할 때 만들고 배치 안에서 재사용합니다.

원본 패치를 정렬하고 겹침을 확인한 뒤, 빌더는 원본과 대체 조각을 순서대로 붙이고 최종 위치로 식별 오프셋을 계산합니다. 보고서는 편집의 내림차순과 양쪽 80바이트의 미리 보기 문맥을 유지합니다. 오른쪽에 이미 적용한 편집도 포함합니다. 저장은 인코딩한 바이트를 트랜잭션 요청으로 옮기면서 길이, 해시, 쓰기 검증, 롤백 데이터를 유지합니다.

Canvas3D는 장면을 교체할 때 배치용 궤도 인덱스를 만듭니다. 기준 궤도 별칭, 정규화해 처음 일치한 다른 궤도와 키가 없는 경우의 대체 경로를 지원합니다. 반복 구조물은 `begin + k * interval`에 인스턴스를 배치하고 Begin마다 다시 시작합니다. 원래 구조물 목록에서 `k % N`으로 모델을 고릅니다. 현재 범위 청크의 double 정밀도 변환은 공통 한도 65,536개까지 캐시합니다. 초과분은 필요할 때 계산합니다. 편집은 이전·새 청크 범위를 무효화하고 장면·청크 교체는 캐시를 초기화합니다. 범위 밖으로 나가면 캐시를 해제합니다. 배치 선형은 궤도 표시 여부와 독립적입니다.

배경 이미지에서 요청 밝기와 업로드한 밝기가 같으면 위치·크기·회전 변경에 텍스처를 재사용합니다. 이미지를 바꾸면 텍스처를 무효화하고 업로드에 성공하면 밝기 기록을 갱신합니다.

편집 시간은 `operation_timing.h`의 steady clock으로 잽니다. GUI와 maploader는 포함 시간 `*_ms`와 횟수 `*_count`를 별도 영역에 기록합니다. GUI 합계에는 DLL 시간이 포함됩니다. App 측정은 지연 속성 창 처리와 필요한 첫 장면 프레임까지 이어집니다. 장면을 숨기거나 접으면 기다림을 즉시 끝냅니다. `source.read_decode_hash`는 호출 스레드의 원본 처리를 기록합니다. 병렬 Include 작업은 `parse.include_join_merge` 및 `parse.syntax_diagnostics`에 포함되며 모델 불러오기는 별도의 비동기 로그를 사용합니다.

## 검증

변경한 구성 요소에 맞는 검사를 고르고 빌드, 계약 테스트, headless, 수동 UI 결과를 구분해 기록합니다. 디스크 쓰기는 저장 및 다시 불러오기 후 내용을 비교합니다. 렌더링, 메뉴, 대화 상자, 끌기 등 화면 조작은 수동으로 확인합니다.

`komapedit.exe`는 GUI 하위 시스템을 사용합니다. PowerShell에서 출력을 수집할 때는 `Start-Process -Wait -WindowStyle Hidden -PassThru`와 `--headless-output`을 사용합니다. 입력 경로를 명시합니다. 일부 기준·다른 궤도, 거리, 반복 구조물, 폐색 구간 모드에서 경로를 생략하면 개발자의 로컬 노선을 사용합니다.

### 성능 벤치마크

전후 비교는 같은 노선, 매개변수, 빌드 유형과 로드 프로필로 수행합니다. 장면, 표 캐시, 편집 벤치마크마다 프로세스 세 개를 순서대로 실행하고 빌드와 다른 벤치마크를 멈춥니다. 지속 프레임의 수용 기준을 확인할 때는 프로파일링을 끕니다.

#### 장면과 로더

`--debug-headless-scene3d-bench`의 기본값은 고정 카메라 300프레임, 25 m 샘플 간격, 뒤 100 m·앞 1200 m 범위, CPU 프레임 p95 16.67 ms 한도입니다. `--interaction moving`은 정상적인 끝점 제한을 적용하며 프레임마다 2 m 이동합니다. 모델을 불러오고 다섯 프레임을 예열한 뒤 측정합니다. 보고서에는 어댑터, 작업량, 가장 느린 프레임, 표시 인스턴스 지문이 포함됩니다.

`--profile-stages`는 중첩 CPU 단계와 비동기 GPU 타임스탬프를 기록합니다. 쿼리는 미리 할당하고 여러 프레임에 걸쳐 읽습니다. 유효하고 준비된 표본을 집계합니다. GPU 시간에는 명령 사이의 간격, CPU 상위 단계에는 하위 단계가 포함됩니다. 단계별 데이터로 병목을 찾고 전체 CPU 프레임 p95로 수용 여부를 판단합니다.

측정 뒤 장면 계약은 직접 계산 경로와 캐시 경로의 double 정밀도 인스턴스 지문, 픽셀, 선택 결과를 비교합니다. 카메라 이동·회전, 청크 이동·복귀, 표시 범위 변경, 안개, 궤도 안내선을 다룹니다. `--debug-headless-scene-loader-contract`는 임시 모델과 headless D3D로 다음을 확인합니다.

- 반복 구조물의 거리와 모델 순서, 타입 목록 반영, Include 순서, 빠진 항목, 길이 0 구간, 선형 이동 기준점, tilt/span, 잘못된 간격, 편집·복원 무효화, 궤도 교체, 캐시 한도
- 모델 복사 및 PutBetween 작업 스레드 실패, 취소, 요청 조정, DLL 할당과 해제의 일치, 같은 경로 모델 재사용과 다시 불러오기
- 유한하고 표현 가능한 모델 경계, 실패 시 정리, 동적 갱신 실패 뒤 반복 구조물 캐시 복원
- 입력이 같거나 위치·크기·회전만 바뀐 경우, 밝기 변경, 이미지 교체, 업로드 실패와 재시도에서 배경 텍스처 식별 정보와 픽셀. 1024×1024 이미지 벤치마크는 선형 적용의 중앙값/p95와 텍스처 교체 횟수를 보고합니다.

#### 평면도와 표

`--debug-headless-plan-bench`는 기본으로 `--interaction pan`을 사용해 캐시와 직접 계산 경로의 곡선, 완화곡선, Interpolate 끝점을 비교합니다. 측정 모드는 히트 결과를 전수 검색과 비교합니다. 작은 점 집합은 선형 탐색, 큰 집합은 공간 격자를 사용합니다. `measure-stationary`는 포인터를 고정하고 `measure-moving`은 결정적인 경로를 따릅니다.

평면도, 장면, 기준 궤도 편집은 임시 Map/Include 계약으로 Interpolate의 인수 0/1/2개 형식, 적용·삭제·초기화, 원본 식별 정보와 고유한 편집 마커를 검증합니다. 실제 노선에서는 사용 가능한 대상을 확인합니다. 빈 행, 이벤트, 마커 집합은 서로 일치해야 하며 인스턴스가 필요한 검사는 해당 없음으로 보고합니다. 기준 궤도 편집은 `rand()`로 원본 파일을 선택하는 맵을 포함해 Preview→Edit 병합도 검사합니다.

`--debug-headless-table-cache-bench`는 편집 메타데이터, 도구 모음 텍스처용 D3D11 장치와 INI 저장을 끈 ImGui/ImPlot 컨텍스트를 사용합니다. 각 샘플은 콜드 캐시 재구축, 웜 캐시 10만 회 호출의 회당 평균 시간, 전체 표를 그리는 한 프레임을 측정합니다. 행, 셀, 식별 정보, 동적 헤더, 너비의 지문을 확인합니다. 원본의 바이트와 해시는 측정 구간 밖에서 검사합니다. 기본값은 반복 5회(범위 1–100), 샘플 간격 25 m입니다. 구조물 모델 검색은 `PASS`, `FAIL`, 또는 이유와 함께 `SKIP`을 출력합니다.

`--debug-headless-diagnostics-popup-bench`는 섞인 로그 100,000건으로 동시 입력 순서, 스냅샷 리비전별 캐싱, 클리핑 렌더링을 검사합니다.

#### 편집

`--debug-headless-edit-bench`는 실제 적용, 지연 삭제, 저장, 되돌리기, 갱신 경로를 호출합니다. 입력 맵에는 편집 가능한 `Structure.Put`, `Repeater.Begin`, `Curve.SetGauge`가 필요합니다. 원본 순서에서 처음 유효한 대상을 골라 정해진 좌표·궤간 증분을 적용합니다. 입력 노선은 메모리 작업과 바이트·해시 확인에 사용합니다. 저장은 상대 의존성을 유지한 배타적 임시 폴더의 일반 파일 사본에 수행합니다. 경로 경계와 재분석 지점을 확인하고 실행 후 정리합니다.

기본값은 `--scene off`, `--repeat 5`(1–100), `--unit-distance 25`입니다. 장면을 켜면 1260×680 캔버스, 뒤 100 m·앞 1200 m 범위, 노선의 최소 거리 + 500 m의 카메라를 사용하며 일반 API에서 범위를 제한합니다. 반복마다 사본을 복원하고 새 App을 만듭니다. 최초 모델을 불러온 뒤부터 필요한 첫 장면 프레임까지 측정합니다. 보고서에는 대상 식별 정보, 중첩 단계, 전체 중앙값·p95·최댓값, 원본 검사, 갱신·롤백 계약이 포함됩니다. 3D 상태마다 프로세스 세 개에서 각각 다섯 번 반복합니다.

### 명령 진입점

```bat
build\komapedit.exe --headless-load-map <map-path> --headless-output build\headless-load-map.txt
build\komapedit.exe --headless-load-scenario <scenario-path> [--scenario-index N] [--expect-no-map] [--scenario-edit-roundtrip] --headless-output build\headless-load-scenario.txt
build\komapedit.exe --debug-headless-scenario-lifecycle <scenario-path> [--scenario-index N] [--unit-distance M] --headless-output build\scenario-lifecycle.txt
build\komapedit.exe --debug-headless-plan-bench <map-path> --interaction pan|measure-stationary|measure-moving --headless-output build\headless-plan-bench.txt
build\komapedit.exe --debug-headless-open-bench <map-path> --repeat 3 --headless-output build\headless-open-bench.txt
build\komapedit.exe --debug-headless-table-cache-bench <map-path> --repeat 5 --unit-distance 25 --headless-output build\table-cache-bench.txt
build\komapedit.exe --debug-headless-scene3d-bench <map-path> --window-back-m 100 --window-forward-m 1200 [--interaction stationary|moving] [--profile-stages] --headless-output build\headless-scene3d-bench.txt
build\komapedit.exe --debug-headless-scene-loader-contract --headless-output build\scene-loader-contract.txt
build\komapedit.exe --debug-headless-diagnostics-popup-bench --headless-output build\diagnostics-popup-bench.txt
build\komapedit.exe --debug-headless-scene-camera-transfer <map-path> --headless-output build\scene-camera-transfer.txt
build\komapedit.exe --debug-headless-source-anchors <map-path> --headless-output build\source-anchors.txt
build\komapedit.exe --debug-headless-station-list-edit <map-path> --headless-output build\station-list-edit.txt
build\komapedit.exe --debug-headless-station-put-margin-edit <map-path> --headless-output build\station-put-margin-edit.txt
build\komapedit.exe --debug-headless-edit-roundtrip <map-path> --headless-output build\edit-roundtrip.txt
build\komapedit.exe --debug-headless-edit-bench <map-path> --scene off|on --repeat 5 --unit-distance 25 --headless-output build\edit-bench.txt
build\komapedit.exe --debug-headless-own-track-edit <map-path> --headless-output build\own-track-edit.txt
build\komapedit.exe --debug-headless-other-track-edit <map-path> [--commit] --headless-output build\other-track-edit.txt
build\komapedit.exe --debug-headless-distance-edit-batch <map-path> --headless-output build\distance-edit-batch.txt
build\komapedit.exe --debug-headless-auto-insert-diagnostics <fixture-directory> [--unit-distance M] --headless-output build\auto-insert-diagnostics.txt
build\komapedit.exe --debug-headless-repeater-edit-batch <map-path> --headless-output build\repeater-edit-batch.txt
build\komapedit.exe --debug-headless-repeater-key-edit <map-path> [--commit] --headless-output build\repeater-key-edit.txt
build\komapedit.exe --debug-headless-other-track-key-edit <map-path> [--commit] --headless-output build\other-track-key-edit.txt
build\komapedit.exe --debug-headless-insert-edit <map-path> --repeater-only [--commit] --headless-output build\repeater-insert-edit.txt
build\komapedit.exe --debug-headless-include-delete <map-path> [--index N] [--commit] --headless-output build\include-delete.txt
build\komapedit.exe --debug-headless-include-replace <map-path> --new-path <file> [--index N] [--commit] --headless-output build\include-replace.txt
build\komapedit.exe --debug-headless-resource-list-replace <map-path> --headless-output build\resource-list-replace.txt
build\komapedit.exe --debug-headless-resource-list-insert <map-path> --kind structure|signal --headless-output build\resource-list-insert.txt
build\komapedit.exe --debug-headless-signal-aspect-columns <map-or-scenario-path> --headless-output build\signal-aspect-columns.txt
build\komapedit.exe --debug-headless-new-file-wizard <nonexistent-map-path-under-tests> --headless-output build\new-file-wizard.txt
build\komapedit.exe --debug-headless-scenario-create <nonexistent-scenario-path-under-tests> --route <existing-map-path> --headless-output build\scenario-create.txt
build\komapedit.exe --debug-headless-fresh-resource-list-workflow <map-path> --headless-output build\fresh-resource-list.txt
build\komapedit.exe --debug-headless-include-import-create <map-path> --headless-output build\include-import-create.txt
build\komapedit.exe --debug-headless-new-element-edit <map-path> [--commit] --headless-output build\new-element-edit.txt
build\komapedit.exe --debug-headless-light-edit <map-path> --headless-output build\headless-light-edit.txt
build\komapedit.exe --debug-headless-pretrain-edit <map-path> --headless-output build\headless-pretrain-edit.txt
build\komapedit.exe --debug-headless-legacy-fog-edit <map-path> --unit-distance 25 --headless-output build\headless-legacy-fog-edit.txt
build\komapedit.exe --debug-headless-sparse-new-element <map-path> --headless-output build\sparse-new-element.txt
build\komapedit.exe --debug-headless-section-edit-batch <map-path> [--commit] --headless-output build\section-edit-batch.txt
build\komapedit.exe --debug-headless-table-find --headless-output build\headless-table-find.txt
build\komapedit.exe --debug-headless-touch-input --headless-output build\headless-touch-input.txt
build\komapedit.exe --debug-headless-settings-persistence --headless-output build\settings-persistence.txt
build\komapedit.exe --debug-headless-curve-parameter-edit <map-path> --headless-output build\curve-parameter-edit.txt
build\bin\typed_snapshot_tests.exe signal-glare <map-path> [--commit]
```

### 리소스 목록과 파일 작업

| 명령 | 입력과 쓰기 범위 | 주요 검사 |
| --- | --- | --- |
| `--debug-headless-resource-list-replace` | 맵 경로. Win32 파일 선택기에서 다른 유효한 Structure List를 직접 선택하고 메모리에 적용 | Preview/Edit 메타데이터 병합, 안정적 식별 정보, 전체 재파싱, 목록 캐시·경로 갱신, 다시 불러온 뒤 원본 해시. 취소, 같은 파일 선택, 잘못된 목록은 FAIL |
| `--debug-headless-resource-list-insert` | 맵 경로와 `--kind structure` 또는 `signal`. 메모리 적용. `--commit`은 거부 | Structure는 Include로 불러온 목록이 필요하며 2개 필드 행과 위·아래 순서를 검사. Signal은 기본·눈부심 삽입 묶음, 6개 필드의 기본 행, 명시적 눈부심 추가를 검사. 둘 다 초기화와 원본 해시 검사 |
| `--debug-headless-signal-aspect-columns` | 맵·시나리오. 입력 노선 편집은 메모리에서 실행하고 저장·다시 불러오기는 임시 픽스처를 사용. `--commit`은 거부 | 실제 열 작업, 확인과 취소, 독립된 기본·눈부심 너비, 적용·되돌리기 반복, 표시 한도 |
| `--debug-headless-new-file-wizard` | `tests/` 아래 아직 없는 맵 경로. 파일을 만들고 실행 뒤 정리 | 빈 맵 다시 불러오기, 배타적 생성, 기존 목록 재사용, 다섯 Load 유형 준비 및 저장, 빈 목록 다시 불러오기 |
| `--debug-headless-scenario-create` | `tests/` 아래 아직 없는 시나리오 경로와 기존 맵을 가리키는 `--route`. 새 시나리오는 실행 뒤 삭제 | 공식 키 순서, 중복 생성 거부, 필드 존재 비트, 상대 경로·기본 가중치, 시나리오 미리 보기, 비동기 맵 불러오기, 기록 보존 |
| `--debug-headless-scenario-lifecycle` | 시나리오. 입력 원본을 읽고 배타적 임시 폴더에 기록 | 기록, 메타데이터 게시, 다시 불러올 때 보기 복원, 단일·복수 Route 선택, 생성·불러오기, 맵 우선 저장, 설정·레이아웃 재시도, CSV 내보내기, 파일 실패 처리 |
| `--debug-headless-fresh-resource-list-workflow` | 맵 경로. 입력을 읽고 임시 폴더에 Map, Structure, Station, Signal 픽스처 생성 | 거리 구문이 없는 맵의 Load 대상. 미저장 Load 여러 개와 빈 목록의 첫 행을 한 배치에 적용. Signal 반복 적용, 눈부심 삭제·재추가, 되돌리기, 인접 행 저장·다시 불러오기. `input_map_bytes_unchanged`와 `fixture_files_cleaned` 확인 |
| `--debug-headless-table-find` | 내장 픽스처 | 역 초안의 일반 Sound 참조와 Sound3D를 구분. 쉼표·공백이 있는 Repeater 키 확인. 찾기 전에 활성 Signal 기본·눈부심 셀을 초안에 반영 |

### 요소 편집과 삽입

`--debug-headless-new-element-edit`는 실제 마법사, 속성 창, 지연 삭제·취소 경로를 사용합니다. 리소스, 반복 구조물, 구조물, 다른 궤도, 곡선·구배 조합 템플릿을 다룹니다. 시작·끝 거리, 완화 구간·캔트 연결, 원본 순서, 대상 파일과 후속 편집을 검사합니다. 리소스 목록 키는 맞는 템플릿의 필드를 채웁니다.

기본 실행은 초기화·다시 불러오기 후 원본 해시를 확인합니다. `--commit`을 지정하면 저장이 짝지은 곡선·구배 구문을 선택한 원본에 쓰고 물리 diff를 확인할 수 있게 남깁니다. 원장이 섞이는 사례에는 편집 가능한 `Structure.Put`과 다른 궤도 위치·X/Y 보간 매개변수 행이 필요합니다. 삽입 순서, x/y 반복 편집, 단일 필드 복원, 적용·되돌리기 실패와 보기 상태를 검사합니다. 관련 저장·새로 불러오기 검사는 임시 픽스처 네 세트를 사용합니다.

`curve.interpolate` 템플릿은 뒤쪽 선택 인수, 공통 타입 검증, 의미 지문을 사용합니다. 공식 인수 0/1/2개 형식을 받으며 캔트만 있는 입력, 유한하지 않은 값, 알 수 없는 필드, 지원하지 않는 메서드는 거부합니다. `typed_edit_contract`는 Shift-JIS/CRLF Include 픽스처에서 모의 실행, 적용·초기화·커밋·다시 불러오기를 수행하며 식, 주석, 순서, 식별 정보를 확인합니다. 새 요소 headless 검증은 인수 두 개 기본값, 연동 체크박스, 후속 편집·취소, 고유한 원본 마커를 확인합니다.

다음 명령은 입력 경로가 필요합니다. 메모리 편집으로 실제 작업 절차를 검증하며 `--commit`은 거부합니다.

| 명령 | 입력 조건 | 검사 |
| --- | --- | --- |
| `--debug-headless-light-edit` | `tests\light_valid.txt` 등 유효한 Light 선언이 일부 포함된 맵 | 메타데이터 병합, 세 형식 모두 적용, 지연 삭제, 거리 0에서 세 마법사 삽입, 계산값과 원본 형태, 원래 선언 집합으로 되돌리기 및 바이트 검사 |
| `--debug-headless-pretrain-edit` | 기존 `PreTrain.Pass`가 있는 맵 | `headless_pretrain.cpp`에서 시각과 초의 반복 적용, 거리, 잘못된 입력, 삭제, 마법사 생성, 삽입 후 편집·취소, 되돌리기, 2D 식별 정보·레이블 및 3D 마커 데이터 검사 |
| `--debug-headless-legacy-fog-edit` | 구형 안개 행이 0개 이상인 맵 | `legacy_fog_edit_validation.cpp`의 식별 정보, 잘못된 입력, 반복 적용, 삭제, 마법사 생성 및 되돌리기. WARP 장면에서 안개 갱신 검사. 표·평면도 캐시와 전체 재구축 예약도 검사 |
| `--debug-headless-curve-parameter-edit` | SetGauge, SetCenter, SetFunction이 있는 맵 | CG/CC/CF 식별 정보, 흰색 표지, 개별 표시, 속성 창의 거리와 매개변수, `SetFunction(2)` 거부, 삭제·생성, 되돌리기, 원본 바이트 |
| `--debug-headless-station-put-margin-edit` | 편집 가능한 `Station.Put`이 있는 맵 | 기존 배치를 골라 거리 보고. 0 또는 부호가 잘못된 허용 범위 거부, 마법사 기본값 `margin1=-5` 및 `margin2=5`, 유효한 삽입, 되돌리기 검사 |
| `--debug-headless-sparse-new-element` | 수치 거리 구문이 0개나 1개이거나 866 미만에서 끝나는 비감소 기준점이 있는 대상 원본 | 실제 `DrawDistance.Change(500)` 마법사를 희소 원본의 거리 25 또는 단조 끝 구간의 866에서 실행. 블록 재사용, 앞·뒤 삽입, 원본 텍스트, 새 행의 식별 정보와 값, 초기화 뒤 해시 확인 |
| `--debug-headless-auto-insert-diagnostics` | `testmap\auto_insert_failures` 픽스처 폴더 | 물리 파일, 줄, 유형, 거리, 식별 정보로 대상 선택. 자동 성공, 수동 복구, 확정 거부, 모든 후보의 실제 적용, 두 번째 적용, 재시도 종료, 초기화 확인. 성공 조건은 `failed_cases=0` 및 `result=PASS` |

PreTrain `passTime`은 따옴표 없는 `hh:mm:ss` 또는 유한한 초를 받습니다. 24시간을 넘는 값과 이전 값 이하의 시각도 포함합니다. 임시 타입 계약 픽스처는 PreTrain과 Legacy.Fog의 인코딩, BOM·줄바꿈, Include, 디스크 동시성, 저장·다시 불러오기를 검사합니다. 자동 삽입 계약은 파일 끝 혼합 배치와 오래된 선택도 다룹니다.

### 연동 편집과 Include

다음 모드에서 `--commit`은 검증된 작업 사본을 저장하고 노선 변경을 diff 확인용으로 남깁니다. 기본 실행은 메모리 검증과 원본 해시 확인을 수행합니다.

| 명령 | 입력 조건과 검증 범위 |
| --- | --- |
| `--debug-headless-repeater-key-edit` | 명시적 맵 경로. 연결 전체 이름 변경 검증 |
| `--debug-headless-other-track-key-edit` | 명시적 맵 경로. 두 구문 이상인 문자열 키의 다른 궤도를 골라 전체 궤도의 원자성, 맵 전체 중복 이름, 의존 참조, 적용·초기화·다시 불러오기, 대상·파일 해시 검사 |
| `--debug-headless-insert-edit --repeater-only` | 고유한 키의 Begin과 Begin0을 각각 생성. 모의 실행과 적용·초기화를 수행하며 커밋할 때 저장·다시 불러오기 검사 |
| `--debug-headless-include-delete` | 명시적 맵 경로. `--index` 기본값 0. 오래된 해시 거부, 하위 트리 삭제, 대상 외 의미, 초기화를 검사. 남는 구문이 제거 대상 하위 트리에 의존하면 삭제 차단 |
| `--debug-headless-include-replace` | 명시적 맵 경로, `--new-path <file>` 및 기본값 0인 `--index`. 경로를 작은따옴표로 감싼 인수로 기록. 기존·새 하위 트리를 제외한 전체 의미, 구조 갱신, 초기화·다시 불러오기를 검사. 이전 변수 의존성과 중복 Load는 차단 |

`--debug-headless-include-import-create`는 명시한 입력 맵 옆에 고유한 이름의 임시 하위 맵을 만듭니다. 기존 파일 가져오기, 파일 만들기, Include 삽입, 거리 0 기준점, 재파싱, 구조 갱신을 검사합니다. 하위 맵은 BOM 없는 UTF-8, CRLF, `BveTs Map 2.02:utf-8` 헤더를 사용합니다. 상위 맵에는 메모리 적용·초기화를 수행합니다. 원본 해시를 확인하고 임시 하위 맵을 제거합니다.

### 제작자 메시지와 설정 저장

`--debug-headless-creator-message <map-or-scenario-path> [--scenario-index N] [--unit-distance M] --headless-output <report>`는 입력을 읽고 원본 파일 바이트를 확인하며 `--commit`을 거부합니다. 별도의 임시 맵과 기록 설정으로 Preview/Edit 메타데이터, 마법사, 원래 내용, 미적용 초안의 저장 차단, 적용·되돌리기·저장·다시 불러오기, Include 메시지, 문서 열기 팝업, 숨김 설정을 검증합니다. DLL 계약은 물리 메시지 중복 제거와 원본 확장 순서를 확인합니다.

`--debug-headless-settings-persistence`는 표준 설정·기록 왕복, 잘못된 항목의 기본값, 읽은 뒤 바이트 보존을 검사합니다. 제작자 메시지의 `has_messages`와 `suppressed`는 `0`/`1`로 저장합니다. 정확히 `1`일 때 true로 읽습니다.

### 수동 확인

관련 항목을 골라 맵·Include 불러오기와 다시 불러오기, 평면도·종단면도·반경도, 역 이동, 측정, CSV 내보내기, 모델 미리 보기와 오류, 3D 객체·마커·카메라·기즈모, 적용·되돌리기·저장·다시 불러오기, 인라인 초안, 설정 저장, Release 배포를 확인합니다. UI에서는 히트 검사와 강조, 메뉴, 확인 대화 상자, 열 스크롤, 최종 렌더링 픽셀을 점검합니다.

## 빌드 스크립트, 종속성, 배포

- CMake가 빌드 구성을 관리하고 배치 스크립트가 Windows 빌드 절차를 실행합니다.
- `NINJA_EXE`, `VCPKG_ROOT`, 기본 `x64-mingw-dynamic` 트리플릿을 유지합니다.
- EXE와 고지 파일은 출력 최상위, DLL은 `bin`, INI는 `settings`에 둡니다.
- 배포 정리는 `bin`, `settings`, `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`를 유지합니다.
- Debug와 Release 빌드, 릴리스 정리는 디렉터리 배치 검사를 공유합니다. 최상위에 구형 INI나 DLL이 있으면 중단합니다.
- ImGui의 도킹 브랜치와 업스트림 ImPlot을 사용합니다.
- 라이선스 및 고지 파일을 보존합니다. 종속성을 추가할 때는 CMake, 개발 문서, 타사 고지를 함께 갱신합니다.
- 노선 배포용 내보내기는 `TODO.md`에 있습니다. Include를 펼치고 선택에 따라 식을 상수로 계산하며 사용 중인 리소스를 복사하고 보고서를 쓰는 기능입니다. 개발 노선을 보호하기 위해 임시 출력 폴더를 사용하도록 계획합니다.
