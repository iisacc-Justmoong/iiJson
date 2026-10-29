# 온톨로지 계약 기반

iiJson의 Ontology 계층은 제품·서비스·도구가 공유할 수 있는 의미 모델과 데이터 교환 계약이다. 도메인의 값, 속성, 객체, 관계, 공통 인터페이스, 액션과 함수의 입출력을 정의하고 실제 데이터 묶음을 검증한다. 검증된 카탈로그와 스냅샷은 소유권이 있는 불변 값이다. 파일·네트워크·데이터베이스·스레드·Core Runtime을 가져오지 않는다.

## 설계 근거와 의존 방향

[Palantir Ontology 개요](https://www.palantir.com/docs/foundry/ontology/overview)는 객체·속성·관계를 의미 요소로, 액션·함수를 동작 요소로 설명하며 인터페이스로 공통 형태와 기능을 다룬다. [타입 참조](https://www.palantir.com/docs/foundry/object-link-types/type-reference)는 객체 타입과 인스턴스를 구분하고, 공용 속성과 값 타입으로 의미와 제약을 재사용한다. iiJson은 이 구성을 모티브로 채택하였다. Palantir의 API나 wire format과 호환되는 구현을 뜻하지 않는다.

모듈 내부 의존 방향은 `iiJson::Ontology → iiJson::iiJson`이다. 두 타깃 모두 다른 SDK를 요구하지 않으며 JSON 파서와 Schema 계층은 Ontology를 참조하지 않는다. Ontology는 기본 JSON을 사용하는 소비자가 선택하여 연결하는 별도 정적 라이브러리이다. 런타임 실행기, 저장소, 인덱싱 서비스, 권한 정책, 추론기 등은 이 계약 위에 별도 모듈로 구현한다.

```cmake
find_package(iiJson 0.1 CONFIG REQUIRED COMPONENTS Ontology)
target_link_libraries(app PRIVATE iiJson::Ontology)
```

공개 헤더는 `<iiJson/Ontology.h>`, 네임스페이스는 `ii::json::ontology`이다. 기본 타깃은 계속 `iiJson::iiJson`이며 `Codec`, `Schema`, `Ontology` CMake 컴포넌트를 검색할 수 있다. 컴포넌트는 설치된 기능 확인용이며 기본 타깃에 Ontology 링크 의존성을 추가하지 않는다.

## 의미 모델

| 구성 요소 | 역할 |
| --- | --- |
| `ValueType` | 이름 있는 값 제약이다. 기존 `Schema`와 메타데이터를 가진다. |
| `PropertyType` | 의미를 재사용하는 공용 속성이다. 값 타입 ID와 의미 메타데이터를 가진다. |
| `PropertyBinding` | 객체에서 사용할 JSON 키를 공용 속성 ID에 연결하며 필수·선택 여부를 정한다. |
| `InterfaceType` | 객체들이 공유하는 속성 계약이다. 다른 인터페이스를 확장할 수 있다. |
| `ObjectType` | 기본키, 속성 바인딩, 구현 인터페이스와 미정의 필드 정책을 가진다. |
| `LinkType` | 방향이 있는 관계 타입이다. 양 끝의 타입 선택자, 양방향 카디널리티와 관계 속성을 가진다. |
| `ActionType` | 동작 이름, 인자·결과 Schema와 선언된 객체·관계 쓰기 대상을 가진다. |
| `FunctionType` | 함수 이름, 인자·결과 Schema와 선언된 객체·관계 읽기 대상을 가진다. |
| `Definition` | 온톨로지 ID, 리비전과 위 타입들을 묶는 소유 데이터이다. |

타입 ID는 각 종류의 이름 공간 안에서 유일해야 한다. 비어 있지 않은 UTF-8 문자열이며 ASCII 공백·제어 바이트를 포함하지 않는다. `urn:iisacc:asset`, `iisacc.creation.Project` 같은 명명을 사용할 수 있다. 문자열 정규화나 URI 해석은 수행하지 않는다. 객체의 JSON 속성 키는 일반 UTF-8 키를 사용할 수 있다.

`metadata`는 JSON 객체이다. 소비자는 `semanticId`, `unit`, `displayName`, 원본 필드 매핑, 공급자 바인딩 등을 기록할 수 있다. iiJson은 이를 보존하지만 해당 키를 자동 실행하거나 의미 추론에 사용하는 것으로 해석하지 않는다. 여러 모듈이 공유하는 의미 키는 소비자 측의 이름 공간과 계약으로 관리한다. 바이너리 값은 명시적인 도메인 인코딩을 먼저 적용해야 한다.

## 카탈로그 컴파일

`Catalog::compile(Definition, OntologyLimits)`는 모든 타입을 등록하고 참조를 해결한 뒤 사용할 수 있는 카탈로그를 반환한다. 실패하면 부분 카탈로그를 반환하지 않는다.

```cpp
#include <iiJson/Ontology.h>
using namespace ii::json;
using namespace ii::json::ontology;

Definition definition;
definition.id = "iisacc.creation";
definition.value_types = {{"Id", Schema::string({1, 64}), {}}};
definition.properties = {{"identity", "Id", {}}};
definition.objects = {{"Asset", "id", {{"id", "identity"}}, {}}};

auto catalog = Catalog::compile(std::move(definition));
// 실패 시 catalog.error().path와 message를 확인한다.
```

컴파일은 중복 ID, 미해결 값·속성·타입 참조, 중복 속성 키, 잘못된 필수 여부·선택자·범위, 존재하지 않는 읽기·쓰기 대상 등을 거부한다. JSON으로 복원할 수 없는 native refinement가 포함된 값 타입이나 호출 계약도 거부한다. 도메인 규칙을 누락한 카탈로그를 내보내지 않는다.

객체의 `primary_key`는 명시적으로 선언된 필수 속성이어야 한다. 값 타입은 null 허용 수정자가 없는 `string`, `integer`, `unsigned_integer` 중 하나이다. 실제 객체의 문자열 식별자는 비어 있을 수 없다. 불리언·실수·null을 식별자로 변환하지 않는다.

카탈로그는 전달된 정의를 소유한다. 원본 `Definition`을 나중에 변경해도 컴파일된 카탈로그의 의미는 바뀌지 않는다. `definition()`은 읽기 전용 정의를 제공하며 UI 편집기·코드 생성기·저장소 어댑터가 이를 검사할 수 있다.

## 인터페이스

인터페이스 상속은 DAG이며 순환을 거부한다. 상속 깊이 상한은 256이고 상속 탐색과 병합은 전체 작업 예산을 소비한다. 같은 키의 공용 속성 ID가 충돌하면 컴파일에 실패한다. 여러 부모의 같은 속성이 필수·선택으로 나뉘면 필수 조건을 유지하며, 자식이 상속받은 필수 조건을 선택으로 약화할 수 없다.

객체 타입은 구현하는 인터페이스의 모든 속성 바인딩을 선언해야 한다. 공용 속성 ID가 같아야 하며 필수 조건을 약화할 수 없다. 임의 Schema 간의 부분 타입 관계를 추정하지 않는다. `Catalog::implements(objectType, interfaceId)`는 상속된 인터페이스까지 확인한다.

관계의 `TypeSelector`는 `SelectorKind::object` 또는 `SelectorKind::interface_type`으로 구체적 객체 타입과 인터페이스를 선택한다. 공개 이름은 `interface` 매크로가 정의된 소비자에서도 충돌하지 않도록 구성한다. 인터페이스를 대상으로 하는 관계는 해당 인터페이스를 직접 또는 간접 구현한 객체를 받을 수 있다.

## 실제 객체와 관계

`Object`는 객체 타입 ID와 속성 객체를 가진다. 식별자는 타입의 기본키 속성에서 추출하므로 별도의 중복 ID 필드를 유지하지 않는다. `ObjectRef`는 객체 타입 ID와 해당 기본키 값이다. 같은 숫자의 비음수 signed/unsigned 저장 차이는 식별자 비교에서 무시하지만 문자열 `"1"`과 숫자 `1`은 구별한다. 64비트 정수를 double로 변환하지 않는다.

`Catalog::validate_object`는 객체 속성과 기본키를 확인하고 `ObjectRef`를 반환한다. `Catalog::snapshot(GraphData)`는 다음을 검사한다.

1. 모든 객체가 알려진 타입이며 해당 속성 계약을 만족한다.
2. `(객체 타입, 기본키)` 식별자가 중복되지 않는다.
3. 모든 관계가 알려진 관계 타입이며 양 끝의 객체가 스냅샷 안에 존재한다.
4. 양 끝의 객체 타입이 구체 타입 또는 인터페이스 선택자를 만족한다.
5. 관계 속성이 계약을 만족하고 동일한 `(관계 타입, 출발 객체, 도착 객체)`가 중복되지 않는다.
6. 양 끝의 관계 수가 최소·최대 조건을 만족한다. 관계가 하나도 없는 객체도 최소 조건을 검사한다.

`targets_per_source`는 출발 객체 한 개가 가질 수 있는 해당 타입의 도착 객체 수이고 `sources_per_target`은 도착 객체 한 개가 가질 수 있는 출발 객체 수이다. `SizeBounds{0, 1}`은 선택적인 단일 관계이다. 기본 상한은 무제한이다. 자기 자신으로 향하는 관계와 객체 관계 그래프의 순환은 허용한다.

스냅샷은 모든 참조가 포함된 닫힌 데이터 묶음이다. 원격 객체를 자동 조회하지 않는다. 페이지 단위 조회 결과나 일부 객체만 포함된 데이터는 완전한 스냅샷과 구분하여 상위 저장소 모듈에서 다루어야 한다. 생성 실패 시 부분적으로 검증된 스냅샷을 반환하지 않는다.

## 탐색

`Snapshot::find(ObjectRef)`는 검증된 인덱스로 객체를 조회하고 읽기 전용 참조를 반환한다. 참조의 수명은 스냅샷 소유권에 종속된다.

`neighbors(ref, linkType, direction, QueryLimits)`는 인접 관계 인덱스를 사용한다. `outgoing`, `incoming`, `both` 방향을 지원하며 관계 타입을 비우면 모든 타입을 탐색한다. 같은 객체가 여러 관계로 연결되거나 자기 관계를 양방향으로 읽어도 결과는 한 번만 반환한다. 결과 순서는 타입 ID와 JSON 식별자 키의 순서이며 숫자 크기 순 정렬을 뜻하지 않는다.

`QueryLimits`의 기본값은 결과 1,000개, 탐색 작업 100,000회이다. 제한을 초과하면 `limit_exceeded`로 실패하며 일부 결과만 반환하지 않는다. SQL·SPARQL·최단 경로·추론·분산 쿼리 기능은 이 API의 범위가 아니다.

## 액션과 함수의 연결 지점

[Palantir의 액션 설명](https://www.palantir.com/docs/foundry/action-types/overview)은 액션을 객체·속성·관계를 함께 변경하는 동작으로 다룬다. iiJson은 해당 동작을 구현할 상위 계층이 공유할 수 있는 계약을 제공한다.

`validate_action(name, arguments)`와 `validate_function(name, arguments)`는 알려진 호출 이름과 입력 Schema를 검사한다. `validate_action_result`와 `validate_function_result`는 결과 Schema를 검사한다. 오류 경로는 `/arguments/...` 또는 `/result/...`이다.

읽기·쓰기 대상은 어떤 타입을 사용하는 동작인지 설명하는 선언이다. 사용자의 실행 권한, 특정 객체에 대한 접근 권한, 외부 작업의 성공이나 트랜잭션 커밋을 증명하지 않는다. 인자 안에 들어 있는 문자열 ID의 실제 존재 여부도 Schema만으로 추정하지 않는다. 실행 공급자는 필요한 객체 조회·권한 검사·업무 규칙·저장 트랜잭션을 수행하고, 입출력 계약과 최종 스냅샷 검증을 이 모듈에 위임할 수 있다.

이 계층은 코드·웹훅·셸·네트워크 호출을 자동 실행하지 않는다. 쓰기 가능한 실행기와 저장소를 교체해도 온톨로지의 데이터 계약을 유지할 수 있도록 의존 방향을 분리한 것이다.

## 버전과 JSON 교환

`Catalog::to_value()`·`Catalog::from_value(value)` 또는 `Codec<Catalog>`를 사용한다. manifest의 envelope은 다음 필드로 구성된다.

```json
{
  "format": "iiJson.Ontology",
  "formatVersion": 1,
  "id": "iisacc.creation",
  "revision": 1,
  "valueTypes": [],
  "properties": [],
  "interfaces": [],
  "objects": [],
  "links": [],
  "actions": [],
  "functions": [],
  "metadata": {}
}
```

각 값 타입과 호출 계약의 Schema는 `iiJson.Schema` 버전 1 정의를 포함한다. 알 수 없는 필드·종류·형식 버전은 오류로 처리한다. 확장 데이터는 선언된 `metadata`에 둔다. 반복 출력과 왕복 읽기에서 정의를 보존하지만 RFC 8785 정규화나 의미적으로 같은 정의의 동일한 해시를 보장하지 않는다.

`Snapshot::to_value()`는 `format: "iiJson.OntologySnapshot"`, `formatVersion: 1`, `ontology: {id, revision}`, `objects`, `links`, `metadata`를 내보낸다. `Catalog::load_snapshot(value)`는 ID와 리비전이 일치하는지 확인한 뒤 전체 무결성을 재검증한다. 리비전은 양의 uint64이며 온톨로지 소유자가 관리한다. 정의의 의미를 바꿀 때 리비전을 올려야 한다. 동일 ID·리비전의 의미를 임의로 바꾸는 것을 원격 저장소 없이 자동 탐지하거나 데이터를 자동 마이그레이션하지 않는다.

RDF·OWL·JSON-LD·외부 JSON Schema 문서의 표준 구현은 아니다. 메타데이터와 안정적인 타입 ID를 기반으로 해당 형식에 연결하는 어댑터나 추론 모듈을 위에 구축할 수 있다.

## 자원과 오류

`OntologyLimits`의 기본값은 JSON `Limits`, 정의 4,096개, 객체 10,000개, 관계 20,000개, 작업 예산 1,000,000회이다. 정의 수는 최상위 타입 선언의 합계이다. 중첩 속성, 인터페이스 탐색, 메타데이터 검사와 Schema 평가도 작업 예산을 사용한다. JSON 깊이·문자열·항목·바이트 제한도 적용한다. 같은 연산의 반복 검증 단계는 하나의 작업 예산을 공유하며 문서 표현별 자원 제한도 유지한다.

값과 속성의 검사에서 `SchemaBudget`를 공유하므로 객체마다 새 검증 예산을 무제한 얻지 못한다. 카디널리티 최소 조건 검사 역시 작업 예산을 소비한다. 호출자 자신이 입력을 구성하거나 복사하는 비용, native 업무 로직과 외부 실행 비용은 이 예산의 범위가 아니다.

오류는 기존 `Result<T>`와 `ErrorCode`를 사용하며 JSON Pointer로 위치를 제공한다. 예를 들어 `/properties/0/valueType`, `/objects/3/properties/id`, `/links/0/target`, `/arguments/id`이다. 기본 JSON 문법 오류의 바이트 위치·행·열은 기존 계약을 유지한다. 메모리 할당 실패 등 일반 C++ 예외는 전파한다.

## 실행 예제와 검증

[creation-ontology.cpp](../example/creation-ontology.cpp)는 Project·Asset, 공용 이름 속성, Named 인터페이스, contains 관계, publish 액션 인자를 구성한다. 카탈로그를 JSON으로 왕복하고 스냅샷을 다시 검증하며 인접 객체를 탐색한다. 실제 배포·게시 부수 효과는 발생시키지 않는다.

```sh
./install.sh
```

이 스크립트는 `build/`에서 Release를 빌드·검사하고 `build/install`에 설치한 뒤 독립 소비자를 빌드·실행한다. 소비자는 Qt·Threads·파일 공급자 탐색을 비활성화하며 기본 JSON 타깃이 Ontology를 역참조하면 실패한다. source JUnit은 `build/json-tests.xml`, 소비자 JUnit은 `build/consumer/json-tests.xml`이다.

주요 실행 경로는 기존 JSON·Schema 검사, `iiJson.SchemaDefinition`, `iiJson.Ontology`, 기존 DTO 예제, `iiJson.OntologyExample`, 의존성 경계 검사이다. 현재 스냅샷의 실행 결과와 설치 파일 대조는 `build/ontology-verification.json`에 기록한다. 다른 플랫폼의 과거 결과를 이번 온톨로지 변경의 실행 근거로 사용하지 않는다.

## 2026-10-27 실행 검증

macOS arm64, AppleClang 21.0, CMake 4.4.4에서 Release 빌드와 CTest 7/7, 스테이징 설치 패키지에만 연결한 독립 소비자 6/6이 통과하였다. ASan·UBSan과 `-Wall -Wextra -Wpedantic -Werror` 설정의 CTest도 7/7 통과하였다. 의도적인 aggregate 기본값 생략에 대한 경고만 별도로 제외하였다.

실행 파일의 계약 검사 수는 JSON 100개, 도메인 Schema 260개, 이식 가능한 Schema 정의 170개, 온톨로지 91개이다. 별도의 DTO 예제와 온톨로지 예제도 실행하였다. 공개 헤더를 `interface` 매크로가 정의된 상태에서 컴파일하는 검사, 64비트 식별자 표현, 사용자 지정 문자열 제한을 따르는 식별자 인덱스, 닫힌 관계 그래프, 인터페이스와 버전 계약을 포함한다.

최종 `build/` 구성과 스테이징 라이브러리는 Release이다. 설치 헤더와 두 정적 라이브러리의 오브젝트 멤버를 소스 빌드 결과와 대조한다. macOS의 CMake 설치 후 `ranlib`가 archive 메타데이터를 바꿀 수 있으므로 전체 archive의 바이트 동일성을 요구하지 않는다. 설치 소비자의 동적 의존성은 시스템 C++ 런타임과 시스템 라이브러리이다.

설치 검증 위치는 저장소 내부 `build/install`이다. `~/.local/SDK/iiJson` 실사용 설치본은 교체하지 않았다. 이번 실행 검증 범위는 macOS arm64이며 Windows·Linux·WASM의 현재 변경 실행을 뜻하지 않는다. `interface` 매크로 검사는 다른 운영체제의 실제 실행 검증과 구분한다.

상세 로그는 `build/ontology-release-install.log`, sanitizer 로그는 `build/ontology-sanitizers-tests.log`이다. 해당 소스 해시, JUnit, 패키지 대조와 의존성 기록은 `build/ontology-verification.json`에 있다.
