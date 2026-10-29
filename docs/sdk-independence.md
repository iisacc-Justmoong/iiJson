# SDK 독립성과 값 이전

iiJson의 JSON 파서·출력기·스키마·온톨로지는 다른 SDK를 찾거나 포함하지 않는다.
`ii::json::Value`, `ValueKind`, `Bytes`, `ValueError`는 이 모듈의 독립적인 타입이다.
문자열과 객체 키는 생성 시 UTF-8을 확인하고 실수는 유한 값만 받는다. 소유 값을
읽기 전용으로 노출하고 정수 부호와 실수 자료형을 명시적으로 보존한다. 바이너리
값은 JSON 텍스트로 자동 변환하지 않는다. JSON 정수 표현 자체는 작고 양수인 값의
원래 부호를 보존하지 못한다.

## 기존 코드 이전

이전 `ii::json::Value`는 `ii::core::Value`의 별칭이었다. 이제 두 타입은 독립적이므로
Core 값을 JSON에 넘기거나 JSON 결과를 Core 값으로 쓰는 코드는 명시적으로 이전하고
라이브러리와 모든 소비자를 다시 빌드해야 한다. `ii::core::ValueKind`, `ii::core::Bytes`
또는 값 접근의 Core 오류를 JSON에 사용하던 코드도 `ii::json` 타입으로 바꾼다.
자료형과 오류 계약이 달라진 ABI를 기존 컴파일 결과와 섞어 사용하지 않는다.

두 모듈을 함께 쓰는 응용 프로그램은 독립적으로 구성한 `example/CoreValue`를
설치하거나 이 어댑터의 `fromCore`·`toCore`를 참고할 수 있다. 어댑터는 두 값을
재귀적으로 깊은 복사하며 부호 있는 정수, 부호 없는 정수, 실수, 문자열, 배열,
객체, 바이너리를 보존한다. 대용량 트리에서는 복사 및 메모리 비용을 고려한다.
양쪽 CMake 패키지를 소비하는 위치는 응용 프로그램이며 iiJson 기본 패키지는 다른
SDK를 검색하거나 연결하지 않는다.

```cmake
find_package(iiJsonCoreValue 0.1 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE iiJson::CoreValue)
```

```cpp
#include <iiJson/CoreValue.h>
auto json_value = ii::json::interop::fromCore(core_value);
auto restored = ii::json::interop::toCore(json_value);
```

## 검증 범위

경계 검사와 Core 검색을 비활성화한 별도 구성·설치 소비자 검사, UTF-8·유니코드
surrogate 및 정수 범위 검사, 유한 binary64 비트 패턴 왕복 검사, 별도의 Core 변환
계약 검사를 수행한다. 결과는 실제 실행한 플랫폼과 함께 기록하며 다른 플랫폼의
실행이나 이전 설치본 교체를 이 검사에서 추정하지 않는다. 다른 SDK의 기존 Core 값
사용은 소비자별 이전이 필요하며 iiJson만 독립화했다고 전체 SDK 이전이 완료되지는 않는다.

2026-10-28 macOS arm64에서 Core·Qt·파일 제공자 검색을 비활성화한 Release 빌드와
기본 계약 검사 6/6, 설치 소비자의 핵심 계약 검사 5/5, 별도 Core 변환 계약 1/1 및
설치된 변환 어댑터 소비자 1/1을 통과했다. 4,096개 비트 패턴 표본 중 유한 값의
왕복을 포함한다. 독립 소비자 실행 파일의 동적 연결은 시스템 C++ 라이브러리와
시스템 라이브러리뿐이었다. 예제 앱은 빌드만 했으며 Windows·Linux·WASM에서 이
변경을 실행하지 않았다. 전역 설치본은 교체하지 않았다.
macOS 12 배포 대상을 지정하고 사용 불가 API 경고를 오류로 처리한 빌드에서
JSON 계약 2/2도 현재 Mac에서 통과했다. 이는 macOS 12 실기기 실행 결과는 아니다.
새 JSON 패키지와 조합한 iiCSMIDI 파일 어댑터의 저작자 계약도 1/1 통과했다.
