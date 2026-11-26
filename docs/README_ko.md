<p align="center">
    <img src="../icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit는 BVE Trainsim 맵 파일을 살펴보고 편집하는 가벼운 Windows용 도구입니다. 2D 노선 평면도, 3D 장면 미리 보기, 원본 파일 기반 맵 편집을 지원합니다.

편집은 실험 기능입니다. 사용하기 전에 노선 파일을 백업하거나 버전 관리에 넣어 두세요. 지원하는 구문은 [BVE 맵 구문 지원 현황](#bve-맵-구문-지원-현황), 개발 진행 상황은 [TODO.md](../TODO.md)에서 확인할 수 있습니다.

## 문서

- [사용자 매뉴얼](user-manual_ko.md)
- [개발 진행 상황](../TODO.md)
- [개발자 가이드](dev_ko.md)
- [AI 코딩 도구 활용 가이드](ai-dev_ko.md)
- [AI 코딩 도구용 저장소 규칙](../AGENTS.md)
- [라이선스](../LICENSE), [프로젝트 고지](../NOTICE), [타사 소프트웨어 고지](../THIRD_PARTY_NOTICES.md)

## 주요 기능

- **인터페이스**는 이동하거나 도킹할 수 있는 창과 검색 가능한 데이터 표를 제공합니다. 표, 2D 보기, 3D 장면 사이에서 위치를 찾을 수 있습니다.
- **미리 보기**에서는 노선 평면도, 종단면도, 마커, 구조물 모델, 3D 장면을 확인할 수 있습니다. 배경 이미지 직접 조정 및 정렬, 측정, 궤도 선형 CSV 내보내기도 지원합니다.
- **편집**에서는 속성 창, 리소스 목록, 마법사로 지원되는 맵 요소를 수정·삭제·추가합니다. 일부 요소는 3D 기즈모로 위치를 조정할 수 있습니다. 하위 맵을 관리하고 맵·시나리오·리소스 목록 파일을 만들거나 편집할 수 있습니다.
- **UI 언어**는 영어, 중국어 간체, 중국어 번체(대만·홍콩), 일본어, 한국어입니다. `언어` 메뉴의 `中文`에 `简体`, `台湾繁體`, `香港繁體`가 있습니다.

## 설치 및 실행

### 릴리스 다운로드(권장)

[GitHub Releases](https://github.com/NewSapporoNingyo/komapedit/releases)에서 미리 빌드된 실행 파일이 들어 있는 압축 파일을 받습니다. 전체 압축을 풀고 `komapedit.exe`를 두 번 클릭합니다.

### 소스에서 빌드

소스 빌드에는 Windows 개발 환경과 빌드 도구가 필요합니다. 소스 빌드에 익숙하지 않다면 미리 빌드된 릴리스를 사용하세요. [개발자 가이드](dev_ko.md)를 따라 빌드한 뒤 `build_release\komapedit.exe`(Release) 또는 `build\komapedit.exe`(Debug)를 실행합니다.

실행 파일과 DLL은 같은 빌드에서 나온 것을 사용합니다. 빌드 스크립트가 출력 폴더 최상위의 구형 INI 또는 DLL을 보고하면 안내에 따라 `bin`과 `settings`에 배치한 뒤 다시 빌드합니다.

### 시작 및 설정

프로그램은 `komapedit.exe` 옆의 `bin` 폴더에서 `maploader.dll`, `model_loader.dll`과 종속 DLL을 불러옵니다.

시작할 때 필요에 따라 `settings` 폴더를 만들고 다음 파일을 생성하거나 읽습니다.

- `settings/imgui.ini`에 UI 창의 위치와 레이아웃을 저장합니다.
- `settings/settings.ini`에 UI 언어, 글자·요소·역 마커 크기, 2D 선 너비, 테마 색상, 편집 모드 경고 상태를 저장합니다. 맵을 열 때 장면 자동으로 불러오기, 안개, 표시 거리, 기즈모 크기, 카메라 속도, 성능 경고 등의 3D 설정도 포함합니다.
- `settings/history.ini`에 최근 맵, 배경 이미지 정렬 매개변수, 맵별 사용자 지정 메시지 표시 설정을 저장합니다.

설정은 인터페이스에서 바꿉니다. 프로그램은 현재 형식의 유효한 항목을 읽고 그 외의 항목에는 기본값을 사용합니다. 저장할 때 전체 설정 파일을 씁니다.

## BVE 맵 구문 지원 현황

- **읽기/미리 보기**는 파싱한 데이터를 궤도 선형, 표, 마커 또는 3D 장면에 표시합니다.
- **기본 편집**은 속성 창, 표, 파일 작업 메뉴에서 기존 내용을 바꾸고 저장합니다.
- **생성**은 맵 요소 만들기 마법사, 새 파일 마법사, 파일 구조도 또는 리소스 표에서 구문, 파일, 목록 행을 만듭니다.
- **그래픽 편집**은 2D/3D 캔버스에서 요소를 끌거나 기즈모로 조작합니다.
- √는 행에 표시된 기능과 형식을 지원합니다. △는 일부 형식, 필드 또는 연동 작업을 지원합니다. ✕는 현재 미지원, -는 해당 없음입니다.

평가는 현재 UI의 모든 작업 경로를 기준으로 합니다. 한 행에 여러 인수 형식이나 별칭이 있으면 일부만 생성할 수 있는 경우 △로 표시합니다. `*.Load` 행은 참조 경로, 목록 파일과 목록 내용을 다룹니다. 자세한 범위는 비고를 확인하세요. 표에는 [공식 BVE 맵 구문](https://bvets.net/jp/edit/formats/route/map.html), 구형 별칭, 별도로 표시한 프로젝트 호환 구문과 주석을 포함합니다.

| 맵 구문 | 읽기/미리 보기 | 기본 편집 | 생성 | 그래픽 편집 | 비고 |
| :--- | :---: | :---: | :---: | :---: | :--- |
| 파일 헤더, 버전, 인코딩 | △ | ✕ | △ | - | UTF-8, UTF-16LE/BE, CP932/Shift_JIS의 BVE Map 2.0 이상을 읽습니다. 새 맵은 `BveTs Map 2.02:utf-8` 헤더를 사용합니다. |
| 주석과 기본 구문 | √ | ✕ | - | - | `#`/`//` 주석, 세미콜론 구분, 키가 있는 요소, 중첩 요소 및 여러 줄 구문을 지원합니다. 이름의 대소문자는 구분하지 않습니다. |
| [프로젝트 주석] `//--kme--message-from-creator:"content"` | √ | √ | √ | - | 일반 BVE 주석에 메시지를 저장합니다. 맵을 열 때 기본으로 표시하며, 사용자 지정 메시지 표에서 편집·삭제하고 마법사의 기타 범주에서 생성할 수 있습니다. |
| 대입, 인수, 키에 쓰이는 변수 | √ | ✕ | ✕ | - | 파싱할 때 계산합니다. 읽기 전용 변수 표에서 이름별로 대입과 원본 위치를 표시합니다. |
| 산술 연산자 (`+`, `-`, `*`, `/`, `%`) | √ | △ | △ | - | 산술 연산, 단항 부호, 괄호, `+` 문자열 결합을 지원합니다. 요소를 편집하거나 만들 때 거리 식 확인 단계에서 수치 식을 입력할 수 있습니다. |
| 거리 선언과 `distance` 식 | √ | △ | △ | △ | 지원되는 요소를 통해 거리 블록을 편집하거나 만듭니다. 거리 식 입력을 요청하면 목표 거리로 계산되는 식을 입력하세요. 일부 3D 요소의 Z축 기즈모는 거리를 1 m 단위로 변경합니다. |
| 수학 함수 | √ | △ | △ | - | `rand`, `abs`, `sin`, `cos`, `atan2`, `sqrt`, `exp`, `log`, `floor`, `ceil`, `pow`를 지원합니다. 거리 식 확인 단계에서 함수를 쓸 수 있습니다. |
| `include 'file';` | √ | √ | √ | - | 파일 구조도에서 하위 맵 참조를 교체·연결 해제·가져오기·생성합니다. 새 파일 마법사에서도 Include를 추가할 수 있습니다. 하위 맵이 없거나 유효하지 않으면 경고를 표시하고 건너뜁니다. |
| `Curve.SetGauge(value)` / [구형] `Curve.Gauge(value)` | √ | √ | △ | ✕ | 거리와 궤간을 편집합니다. 마법사는 기본값 `1.067`로 `SetGauge`를 만듭니다. 2D/3D에서 `CG` 표지를 표시할 수 있습니다. |
| `Curve.SetCenter(x)` | √ | √ | √ | ✕ | 거리와 캔트 회전 중심의 오프셋을 편집합니다. 생성 기본값은 `0`입니다. 2D/3D에서 `CC` 표지를 표시할 수 있습니다. |
| `Curve.SetFunction(id)` | √ | √ | √ | ✕ | 거리와 보간 함수의 `id`를 `0` 또는 `1`로 편집합니다. 생성 기본값은 `0`입니다. 2D/3D에서 `CF` 표지를 표시할 수 있습니다. |
| `Curve.BeginTransition()` | √ | △ | △ | ✕ | 짝이 되는 Begin/End에서 기존 완화곡선의 시작 거리를 편집합니다. 캔트가 있는 Begin이나 End와 함께 만들 수 있습니다. 시작과 종료의 완화곡선 옵션은 연동되며 거리는 각각 지정합니다. |
| `Curve.Begin(radius, cant)` / [구형] `Curve.BeginCircular(radius, cant)` | √ | √ | △ | ✕ | 마법사는 캔트가 있는 Begin과 그 앞의 완화곡선을 함께 만듭니다. 기존 구형 BeginCircular 구문도 편집할 수 있습니다. |
| `Curve.Begin(radius)` / `Curve.Change(radius)` | √ | √ | √ | ✕ | 마법사에서 Begin 또는 Change를 선택하고 종료 위치를 함께 추가할 수 있습니다. |
| `Curve.End()` | √ | √ | √ | ✕ | 단독으로 또는 시작 위치와 함께 만듭니다. 앞에 완화곡선을 둘 수 있습니다. |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()` | √ | √ | √ | ✕ | 0/1/2개 인수를 지원하며 편집할 때 인수 개수를 유지합니다. 곡선 반경을 표시하면 2D/3D 마커에서 편집하거나 삭제할 수 있습니다. |
| `Gradient.BeginTransition()` | √ | △ | △ | ✕ | 짝이 되는 Begin/End에서 기존 완화 구간의 시작 거리를 편집합니다. Begin/End와 함께 만들 수 있습니다. 시작과 종료의 완화 구간 옵션은 연동되며 거리는 각각 지정합니다. |
| `Gradient.Begin(gradient)` / [구형] `Gradient.BeginConst(gradient)` | √ | √ | △ | ✕ | 마법사에서 Begin을 단독으로 추가하거나 End와 선행 완화 구간을 함께 추가합니다. 기존 구형 BeginConst 구문도 편집할 수 있습니다. |
| `Gradient.End()` | √ | √ | √ | ✕ | 단독으로 또는 Begin과 함께 만듭니다. 앞에 완화 구간을 둘 수 있습니다. |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()` | √ | ✕ | ✕ | ✕ | 인수 0개와 1개 형식 모두 궤도 선형에 반영합니다. |
| `Legacy.Turn`, `Legacy.Curve`, `Legacy.Pitch` | √ | △ | ✕ | ✕ | 모두 기준 궤도 선형에 쓰이는 프로젝트 호환 구문입니다. 기존 Legacy.Curve의 값과 거리는 편집할 수 있습니다. |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()` | √ | △ | √ | ✕ | 모든 인수 형식을 지원합니다. 거리와 값을 편집하거나 구문을 삭제할 수 있습니다. 각 구문의 `trackKey`는 읽기 전용입니다. 궤도 전체의 이름은 다른 궤도 표에서 바꿉니다. |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()` | √ | △ | √ | ✕ | 표에 있는 인수 형식을 모두 지원합니다. 편집 범위는 X.Interpolate와 같습니다. |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)` | √ | △ | √ | ✕ | 모든 인수 형식을 지원합니다. 인수 개수를 유지하면서 거리와 값을 편집하거나 구문을 삭제합니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Track[trackKey].Cant.SetGauge(gauge)` / [구형] `Track[trackKey].Gauge(gauge)` | √ | △ | △ | ✕ | 새 구문은 Cant.SetGauge를 사용합니다. 두 형식 모두 값과 거리를 편집하거나 삭제할 수 있습니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Track[trackKey].Cant.SetCenter(x)` | √ | △ | √ | ✕ | 값과 거리를 편집하거나 구문을 삭제합니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Track[trackKey].Cant.SetFunction(id)` | √ | △ | √ | ✕ | 새 구문의 `id`에는 `0` 또는 `1`을 사용합니다. 값과 거리를 편집하거나 삭제할 수 있습니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Track[trackKey].Cant.BeginTransition()` | √ | △ | √ | ✕ | 거리를 편집하거나 구문을 삭제합니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Track[trackKey].Cant.Begin(cant)` | √ | △ | √ | ✕ | 값과 거리를 편집하거나 구문을 삭제합니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Track[trackKey].Cant.End()` | √ | △ | √ | ✕ | 거리를 편집하거나 구문을 삭제합니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / [구형] `Track[trackKey].Cant(cant)` | √ | △ | △ | ✕ | 인수 0개 또는 1개의 Interpolate를 생성합니다. 인수 개수를 유지하면서 기존 값과 거리를 편집하거나 삭제할 수 있습니다. 키 이름은 X.Interpolate와 같은 방식으로 바꿉니다. |
| `Structure.Load(filePath)` | √ | √ | √ | - | 목록 경로를 바꾸거나 목록을 생성·가져온 뒤 Load 참조를 추가합니다. 키/경로 행을 편집·추가·삭제·재정렬할 수 있고, 빈 목록에 첫 행도 넣을 수 있습니다. 목록 버전 1.00 이상을 지원합니다. |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)` | √ | √ | √ | △ | 모든 필드를 편집할 수 있습니다. 3D 기즈모로 X/Y/Z를 조정합니다. |
| `Structure[structureKey].Put0(trackKey, tilt, span)` | √ | √ | √ | △ | 속성/편집에서 Put0과 Put을 서로 변환합니다. Put0의 Z축 기즈모는 거리를 1 m 단위로 바꿉니다. |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)` | √ | √ | △ | △ | 두 형식 모두 편집할 수 있습니다. 새 구문은 인수 3개를 사용합니다. 속성 입력 중에 3D 모양이 갱신되며, Z축 기즈모로 거리를 1 m 단위로 조정합니다. |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` | √ | √ | √ | △ | 모든 매개변수와 구조물 키 목록, 연결된 구문 전체의 키를 편집합니다. Begin/Begin0 변환, 변경점, 연결 구문 삭제, 위치 기즈모를 지원합니다. 단독으로 또는 End와 짝을 지어 만듭니다. 같은 키의 닫힌 구간은 서로 겹칠 수 없습니다. |
| `Repeater[repeaterKey].End()` | √ | △ | √ | △ | 시작을 정의한 Begin이나 End 기즈모로 종료 거리를 편집합니다. 단독으로 또는 짝을 지어 만듭니다. 단독 End에는 별도 속성 창이 없으며, 같은 키의 닫힌 구간 안에는 End를 더 넣을 수 없습니다. |
| `Background.Change(structureKey)` | √ | √ | √ | ✕ | 거리와 키를 편집합니다. 장면에서 배경을 미리 볼 수 있습니다. |
| `Station.Load(filePath)` | √ | √ | √ | - | 목록 경로를 바꾸거나 목록을 생성·가져온 뒤 Load 참조를 추가합니다. 13개 필드를 모두 편집하고 빈 목록의 첫 행을 포함해 역 행을 추가·삭제·재정렬합니다. 목록 버전 0.04 이상을 지원합니다. |
| `Station[stationKey].Put(door, margin1, margin2)` | √ | √ | √ | ✕ | 거리, 키, 출입문 방향, 정차 허용 범위를 편집합니다. `margin1 < 0`, `margin2 > 0`이어야 합니다. |
| `Section.Begin(...)` / [구형] `Section.BeginNew(...)` | √ | √ | √ | ✕ | 거리와 신호 인덱스 목록을 편집합니다. 2D/3D에서 마커를 표시합니다. |
| `Section.SetSpeedLimit(...)` / [구형] `Signal.SpeedLimit(...)` | √ | √ | √ | ✕ | 거리와 제한 속도 목록을 편집합니다. 유효 값은 3D 신호 요약에 표시합니다. |
| `Signal.Load(filePath)` | √ | △ | √ | - | 현시 목록을 교체·생성·가져오고 Load 참조를 추가합니다. 기본 행, 눈부심 행, 열을 추가할 수 있습니다. 구조물 키는 최대 509열을 표시합니다. 눈부심 행이 여러 개면 유지하는 행들의 총 필드 수를 보존해야 합니다. |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)` | √ | √ | △ | △ | 두 형식 모두 편집할 수 있으며 새 구문은 전체 형식을 사용합니다. 짧은 형식을 확장할 때 변환을 확인합니다. 3D 기즈모로 X/Y/Z를 조정합니다. |
| `Beacon.Put(type, section, sendData)` | √ | √ | √ | ✕ | 거리와 모든 매개변수를 편집합니다. |
| `SpeedLimit.Begin(v)` / `SpeedLimit.End()` | √ | √ | √ | ✕ | Begin/End를 각각 편집·생성·삭제합니다. |
| `PreTrain.Pass(time)` / `PreTrain.Pass(second)` | √ | √ | √ | ✕ | 2D/3D 마커에서 거리와 통과 시각을 편집하거나 삭제합니다. 마법사의 신호 범주에서 만듭니다. 시각은 hh:mm:ss 또는 초 단위로 입력합니다. `passTime`에 마우스를 올리면 형식 안내가 표시됩니다. |
| `Light.Ambient(...)`, `Light.Diffuse(...)`, `Light.Direction(...)` | √ | √ | √ | ✕ | 광원 표에서 매개변수를 확인하고 편집합니다. 마법사의 효과 범주에서 거리 `0`에 만듭니다. RGB 범위는 `[0, 1]`이며 Direction은 거리 `0`이 필요합니다. 루트 맵과 Include 전체에서 종류별 구문은 하나만 허용합니다. |
| `Fog.Interpolate(density, red, green, blue)` / `Fog.Interpolate(density)` / `Fog.Interpolate()` / [구형] `Fog.Set(density, red, green, blue)` | √ | √ | √ | ✕ | 0/1/4개 인수의 Interpolate와 구형 Set을 지원합니다. 3D에서 지수 안개와 전환을 미리 봅니다. |
| [호환] `Legacy.Fog(start, end, red, green, blue)` | √ | √ | √ | ✕ | 표 또는 2D/3D 마커에서 편집합니다. 3D에서 선형 안개와 전환을 미리 보며 Fog와 함께 사용할 수 있습니다. |
| `DrawDistance.Change(value)` | √ | √ | √ | ✕ | 거리와 값을 편집합니다. 장면의 표시 거리를 제어할 수 있습니다. |
| `CabIlluminance.Interpolate(value)` / `CabIlluminance.Interpolate()` / [구형] `CabIlluminance.Set(value)` | √ | √ | √ | ✕ | 거리와 조도를 편집합니다. 값이 비어 있으면 Interpolate()를 기록합니다. 표와 3D 표지는 이전 유효 값을 표시하며 이전 값이 없으면 비워 둡니다. |
| `Irregularity.Change(x, y, r, lx, ly, lr)` | √ | √ | √ | ✕ | 표와 마커로 확인합니다. 거리와 6개 매개변수를 모두 편집합니다. |
| `Adhesion.Change(a)` / `Adhesion.Change(a, b, c)` | √ | √ | √ | ✕ | 표와 마커로 확인합니다. 인수 1개와 3개 형식을 지원합니다. |
| `Sound.Load(filePath)` | √ | √ | √ | - | 목록 경로를 바꾸거나 목록을 생성·가져온 뒤 Load 참조를 추가합니다. 키, 경로, 버퍼 수를 편집하고 빈 목록의 첫 행을 포함해 행을 추가·삭제·재정렬합니다. 목록 버전 2.00 이상을 지원합니다. |
| `Sound[soundKey].Play()` | √ | √ | √ | ✕ | 표와 마커로 소리 이벤트를 확인하고 거리와 키를 편집합니다. |
| `Sound3D.Load(filePath)` | √ | √ | √ | - | 3D 소리 목록을 교체·생성·가져오고 Load 참조를 추가합니다. 키, 경로, 버퍼 수를 편집하고 빈 목록의 첫 행을 포함해 행을 추가·삭제·재정렬합니다. 목록 버전 2.00 이상을 지원합니다. |
| `Sound3D[soundKey].Put(x, y)` | √ | √ | √ | △ | 거리, 키, X/Y를 편집합니다. 3D 표지로 음원 위치를 표시하고 기즈모로 X/Y 오프셋과 1 m 단위 거리를 조정합니다. |
| `RollingNoise.Change(index)` | √ | √ | √ | ✕ | 표와 마커로 소음 이벤트를 확인하고 거리와 인덱스를 편집합니다. |
| `FlangeNoise.Change(index)` | √ | √ | √ | ✕ | 표와 마커로 소음 이벤트를 확인하고 거리와 인덱스를 편집합니다. |
| `JointNoise.Play(index)` | √ | √ | √ | ✕ | 표와 마커로 소음 이벤트를 확인하고 거리와 인덱스를 편집합니다. |
| `Train.Add(trainKey, filePath, trackKey, direction)` / `Train[trainKey].Load(filePath, trackKey, direction)` | △ | ✕ | ✕ | ✕ | 외부 정의 파일을 일부 읽어 타 열차 정의를 표시합니다. |
| `Train[trainKey].Enable(time)` / `Train[trainKey].Enable(second)` | √ | ✕ | ✕ | ✕ | 타 열차 정차 지점 표 위에 활성화 시각을 표시합니다. |
| `Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)` | √ | ✕ | ✕ | ✕ | 읽기 전용 타 열차 정차 지점 표, 경로, 맵 마커를 표시합니다. |

## 라이선스 및 타사 소프트웨어 고지

komapedit는 Apache License, Version 2.0에 따라 배포됩니다. 라이선스 전문은 `LICENSE`, 프로젝트 저작권 고지는 `NOTICE`를 확인하세요.

이 프로젝트는 BVE Trainsim 맵 파일을 살펴보고 편집하기 위해 `kobushi-trackviewer`를 기반으로 개발되었습니다.

| 기반 프로젝트 | 저작권 | 라이선스 |
| --- | --- | --- |
| konawasabi의 [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

GUI와 모델 미리 보기에 쓰이는 타사 라이브러리입니다.

| 라이브러리 | 용도 | 저작권 | 라이선스 |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | 도킹 GUI, Win32 백엔드, DirectX 11 백엔드, C++ std::string 도우미 | Copyright (c) 2014-2026 Omar Cornut | MIT License |
| [ImPlot](https://github.com/epezent/implot) | 2D 차트 위젯 | Copyright (c) 2020-2024 Evan Pezent, Copyright (c) 2025-2026 Breno Cunha Queiroz | MIT License |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | 구조물 모델 가져오기 | Copyright (c) 2006-2026, assimp team | Modified BSD 3-Clause License |
| Dear ImGui에 포함된 stb 단일 파일 라이브러리 | 글꼴·텍스트·사각형 패킹 지원 | Copyright (c) 2017 Sean Barrett | MIT License 또는 Public Domain |

이 저장소의 소스 또는 빌드한 바이너리를 배포할 때 `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`를 함께 포함합니다. `third_party/` 소스 트리를 배포할 때는 원래 라이선스 파일과 저작권 고지를 유지합니다.

[프로젝트 페이지](https://github.com/NewSapporoNingyo/komapedit)

## GitHub 스타 추이

<a href="https://www.star-history.com/?repos=NewSapporoNingyo%2Fkomapedit&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
   <img alt="GitHub 스타 추이 차트" src="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
 </picture>
</a>
