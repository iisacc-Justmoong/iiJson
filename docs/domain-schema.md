# 도메인 스키마

iiJson의 `Schema`는 도메인 데이터의 구조와 제약을 명시하는 불변 값 객체이다. JSON 문법을 읽고 쓰는 코덱 위에서 필수 필드, 자료형, 중첩 구조, 범위, 버전 및 업무 규칙을 검증한다. 도메인 이름이나 DTO를 전역 레지스트리에 등록하지 않으며 제품별 스키마는 해당 소비자 모듈이 소유한다. 모듈은 C++23 표준 라이브러리를 사용하며 다른 SDK에 의존하지 않는다.

[Qt의 JSON API](https://doc.qt.io/qt-6/json.html)는 `QJsonDocument`, `QJsonObject`, `QJsonArray`, `QJsonValue`를 통해 구조화된 데이터를 다룬다. iiJson은 기존 Core `Value`를 데이터 트리로 사용하고 그 위에 명시적 도메인 계약을 추가한다. 외부 JSON Schema 문서를 읽는 표준 검증 엔진은 아니며 `$ref` 해석, 원격 문서 로딩, 리플렉션 기반 DTO 생성은 제공하지 않는다.

## 정의와 검증

```cpp
#include <iiJson/Schema.h>
using namespace ii::json;

const auto track = Schema::object({
    Field::required("name", Schema::string({1, 80})),
    Field::optional("muted", Schema::boolean()),
    Field::optional("comment", Schema::string().nullable())
});
const auto project = Schema::object({
    Field::required("schemaVersion", Schema::literal(Value(1))),
    Field::required("tracks", Schema::array(track, {0, 1000})),
    Field::optional("state", Schema::enumeration({Value("draft"), Value("published")}))
});

auto document = parse(R"({"schemaVersion":1,"tracks":[{}]})", project);
// document.error().code == ErrorCode::schema_mismatch
// document.error().path == "/tracks/0/name"

Value value(Value::Object{{"schemaVersion", Value(1)}, {"tracks", Value(Value::Array{})}});
auto valid = project.validate(value);
auto same = validate(value, project);
auto text = stringify(value, project);
```

`parse(text, schema, limits)`는 문법 검사 후 스키마를 검증하며 실패하면 부분 문서를 반환하지 않는다. `stringify(value, schema, limits)`는 스키마 검사 후 출력한다. 기존 `parse(text, limits)`와 `stringify(value, limits)`, `{}`를 전달하는 호출은 유지된다.

## 구성 요소

| API | 계약 |
| --- | --- |
| `Schema::any()` 또는 기본 생성자 | JSON으로 표현 가능한 임의의 값이다. 자원 제한과 바이너리 거부는 유지한다. |
| `null()`, `boolean()` | 해당 자료형만 허용한다. |
| `integer(minimum, maximum)` | signed 64비트 범위의 정수를 허용한다. unsigned 저장 변형도 해당 범위이면 허용한다. |
| `unsigned_integer(minimum, maximum)` | unsigned 64비트 범위의 정수이다. signed 저장 변형의 비음수도 허용한다. |
| `number(minimum, maximum)` | 정수와 실수를 허용한다. 생략한 경계는 제한하지 않으며 양쪽 경계를 포함한다. |
| `string(SizeBounds)` | 유니코드 스칼라 값 개수를 검사한다. 그래핌이나 UTF-8 바이트 개수와 다르다. |
| `array(element, SizeBounds)` | 원소 개수와 모든 원소의 스키마를 검사한다. |
| `object(fields, AdditionalFields)` | 이름별 필드 스키마와 필수 여부를 검사한다. 기본값은 미정의 필드 거부이다. |
| `literal(value)`, `enumeration(values)` | 지정된 JSON 값과 일치해야 한다. 구조화된 값도 허용한다. |
| `one_of(alternatives)` | 정확히 하나의 대안에 일치해야 한다. 두 개 이상 일치해도 실패한다. |
| `nullable()` | 원본을 변경하지 않고 명시적 null도 허용하는 정의를 만든다. null에서는 나머지 제약과 refinement를 실행하지 않는다. |
| `refine(validator)` | 기본 구조가 검증된 뒤 추가 업무 규칙을 실행한다. |

`SizeBounds{minimum, maximum}`는 양 끝을 포함하며 기본값은 `0`부터 `size_t` 최댓값이다. 필드 누락과 null은 별개이다. `Field::optional`은 누락을 허용하고 `nullable()`은 실제로 존재하는 null을 허용한다. 필드에 기본값을 삽입하거나 입력 데이터를 수정하지 않는다. 미정의 필드를 허용하려면 `AdditionalFields::allow`를 명시한다. 허용된 필드도 JSON 자원 검사를 통과해야 한다.

자료형을 강제 변환하지 않는다. `integer()`는 `1.0`, `"1"`, `true`를 거부한다. 정수 스키마는 signed/unsigned 저장 변형 간 범위를 검사하지만 `Value`를 바꾸지 않는다. DTO 및 refinement는 `Value::get<T>()`의 정확한 자료형 추출 계약을 따라야 한다. `number()`의 실수 경계와 64비트 정수 비교에서도 정수를 double로 반올림하지 않는다.

열거형은 같은 비음수 정수의 signed/unsigned 저장 차이를 무시한다. 그 외 자료형은 일치해야 하므로 정수 `1`과 실수 `1.0`은 서로 다른 literal이다. 배열과 객체 내부에도 이 규칙이 적용된다. 객체 순서는 Core `Value::Object`와 같이 키 순서이다.

잘못된 정의는 프로그래밍 오류로 취급하여 생성 시 `std::invalid_argument`를 발생시킨다. 뒤집힌 범위, 무한대·NaN 경계, 중복 필드명, 잘못된 UTF-8 이름, 빈 열거형·대안 목록, 바이너리 literal, 비어 있는 refinement는 허용하지 않는다. 스키마를 복사하거나 `nullable()`·`refine()`으로 조합해도 기존 정의는 유지된다.

## Codec와 연결

스키마 자체도 `Schema::definition()`으로 `Value` 정의를 내보내고 `Schema::from_definition(value)`로 복원할 수 있다. `Codec<Schema>`를 제공하므로 `encode(schema)`·`decode<Schema>(text)`도 사용할 수 있다. wire envelope은 `format: "iiJson.Schema"`, `formatVersion: 1`, `schema` 세 필드이다. 각 노드는 `kind`와 종류별 제약을 가진다. 필수 필드명·존재 여부·null 허용·자료형·범위·열거형·대안의 의미를 왕복 보존한다. 알 수 없는 필드와 버전은 거부한다.

정의에서 문자열·배열 길이의 상한을 생략하면 무제한이다. 이를 `SIZE_MAX` 리터럴로 내보내지 않으므로 32비트와 64비트 플랫폼 사이에서 기본 상한을 공유할 수 있다. 실수 경계로 가져오는 정수는 binary64에서 정확히 표현할 수 있어야 한다. 읽기 과정에서 64비트 정수 경계를 임의로 반올림하지 않는다.

`refine()`의 native 콜백은 JSON으로 복원할 수 없으므로 해당 스키마나 이를 포함한 중첩 스키마의 정의 출력은 `unsupported_value`로 실패한다. 검증 규칙을 버린 채 성공하지 않는다. 온톨로지는 이 이식 가능한 정의를 값 타입과 호출 계약에 재사용하며, native 업무 로직은 상위 실행 모듈이 담당한다. 상세 구조는 [온톨로지 계약](ontology.md)에 있다.

`Schema::kind()`는 최상위 스키마 종류, `nullable_enabled()`는 명시적인 null 허용 수정자의 상태를 반환한다. 후자는 `any()`나 `null()`의 일반적인 null 수용 여부를 계산하는 함수가 아니다.

여러 검증 호출에 연산 예산을 공유하려면 `SchemaBudget budget{totalSteps}`를 만들고 `schema.validate(value, limits, budget)`를 호출한다. 각 호출의 `Limits::schema_steps`와 누적 `budget.remaining_steps`를 함께 검사한다. 실패할 때도 이미 사용한 예산은 복원하지 않는다. Ontology는 이를 이용하여 여러 객체의 검증이 각각 새 예산으로 무제한 반복되지 않게 한다.

기존 `Codec<T>::encode`·`decode`에 `static const Schema& schema()` 또는 `static Schema schema()`를 추가하면 텍스트 도우미가 이를 자동으로 적용한다.

```cpp
template<> struct ii::json::Codec<Project> {
    static const Schema& schema();
    static Result<Value> encode(const Project& value);
    static Result<Project> decode(const Value& value);
};

auto project = ii::json::decode<Project>(input);
auto output = ii::json::encode(projectValue);
```

읽기는 JSON 파싱 → 스키마 검증 → DTO 변환 순서이다. 쓰기는 DTO 변환 → 스키마 검증 → JSON 출력 순서이다. 검증 실패 시 DTO 읽기 함수는 호출하지 않으며 잘못된 DTO의 출력도 거부한다. `schema()`가 없는 기존 Codec의 동작은 유지된다. `Codec<T>`의 변환 함수를 직접 호출하면 도우미 검증을 거치지 않으므로 호출 측이 전제 조건을 책임진다. 완전한 변환 구현은 [실행 예제](../example/domain-schema.cpp)에 있다.

## 업무 규칙과 오류

`Schema::Validator`는 `Result<void>(const Value&)` 콜백이다. 실패 시 `ErrorCode::schema_mismatch`와 설명을 반환한다. 구조 검증에 실패하면 콜백을 실행하지 않는다. 콜백의 `Error::path`는 현재 값에 대한 상대 JSON Pointer이며 바깥의 경로와 결합된다. `/range` 필드의 규칙이 `/end`를 반환하면 최종 경로는 `/range/end`이다. 비어 있지 않은 경로는 `/`로 시작하는 UTF-8 문자열이며 `~` 이스케이프도 유효해야 한다. 잘못된 콜백 경로는 `std::invalid_argument`를 발생시킨다. `one_of`는 `schema_mismatch`만 대안 불일치로 취급하며 다른 오류는 즉시 전파한다.

스키마 오류의 `offset=0`, `line=1`, `column=1`은 원문 위치를 나타내지 않는다. `path`가 데이터 트리에서의 위치이며 빈 문자열은 루트이다. 필드명의 `~`와 `/`는 RFC 6901에 따라 `~0`과 `~1`로 이스케이프한다. 누락 필드도 해당 필드의 경로를 반환한다. JSON 문법 오류는 기존 바이트 위치·행·열을 유지하고 `path`는 비어 있다. 오류는 결정적인 순서로 첫 하나를 반환한다.

스키마 정의는 불변 공유 상태이다. 콜백에 캡처한 외부 상태의 수명, 재진입성과 동시 호출 안전성은 도메인 모듈이 책임진다. 콜백은 데이터가 같으면 같은 결과를 반환해야 하며 `one_of` 평가에서 여러 번 호출될 수 있다. 할당 실패나 콜백이 발생시키는 일반 예외는 호출자에게 전파한다.

## 자원 제한

`Schema::validate(value, limits)`는 모든 값과 객체 키를 검사한다. `any()`나 허용된 미정의 필드도 `Limits::depth`, `items`, `string_bytes`를 우회하지 못하며 iiJson 바이너리 값은 `unsupported_value`로 거부한다. 직접 Value를 검증할 때 `input_bytes`·`output_bytes`는 적용 대상이 없다. 텍스트 도우미를 사용하면 기존 파서·출력기가 해당 제한을 적용한다.

`Limits::schema_steps`는 기본 100,000회이며 문서의 `items` 예산과 독립적이다. 스키마 노드 평가, 필드 확인, literal 비교와 추가 규칙 호출이 이 예산을 소비한다. `one_of`의 대안 평가도 같은 예산을 사용하므로 분기 수가 많아도 무제한 탐색하지 않는다. 데이터 깊이 기본값은 64, 데이터와 스키마 재귀의 상한은 각각 256이다. 제한 초과는 `limit_exceeded`를 반환한다. 콜백 내부 계산과 스키마 생성에 드는 비용은 호출 측 책임이다.

공개 `Error`에 `path`, `Limits`에 `schema_steps`가 추가되었으므로 새 헤더와 정적 라이브러리는 함께 사용해야 한다. 기존 바이너리와 새 헤더를 혼용하지 말고 소비자도 다시 빌드한다.

## 빌드 및 검증

```sh
./install.sh
```

기본 설치 위치는 저장소의 `build/install`이다. 첫 번째 인자로 다른 설치 prefix를 지정할 수 있다. 스크립트는 `build/`의 Release 빌드와 CTest를 완료한 뒤 설치하고 `build/consumer`의 독립 소비자를 빌드·실행한다. 소비자는 Qt, Threads, 파일 공급자와 Core 탐색을 비활성화하며 Core 타깃이 유입되면 구성 단계에서 실패한다.

스크립트는 소스 빌드 결과를 `build/json-tests.xml`, 독립 소비자 결과를 `build/consumer/json-tests.xml`에 JUnit 형식으로 기록한다. 테스트 실행 실패 시 설치 또는 후속 검증으로 진행하지 않는다.

`iiJson.Schema`는 중첩 오류 경로, 필수·선택·null 구분, 정수 경계, 유니코드 길이, 미정의 필드, 대안 중복, 사용자 규칙, 자원 제한, 정의 오류, Codec 자동 검증 및 기존 호출 호환성을 검사한다. `iiJson.Contracts`는 기존 JSON 문법과 수동 Codec 계약을 유지한다. `iiJson.DomainExample`은 실제 도메인 DTO의 왕복 변환을 실행한다. 설치 소비자는 같은 세 실행 경로를 설치된 헤더와 라이브러리로 다시 검증한다.

## 2026-10-27 검증 기록

이 절은 최초 도메인 스키마 구현 시점의 기록이다. 이후 추가된 스키마 정의와 온톨로지의 현재 검증 범위는 [온톨로지 문서](ontology.md) 및 `build/ontology-verification.json`을 기준으로 확인한다.

macOS arm64, AppleClang 21.0, CMake 4.4.4에서 다음 경로를 실행하였다.

- Release 빌드 및 CTest: 4/4 통과이다. 스키마 실행 파일은 260개 계약 검사를 수행한다.
- AddressSanitizer·UndefinedBehaviorSanitizer 구성: CTest 4/4 통과이다. 검사 후 최종 `build/` 구성은 Release로 복원하였다.
- `build/install`에 스테이징한 헤더·정적 라이브러리만 사용하는 독립 소비자: 3/3 통과이다. 선행 Core는 `~/.local/SDK/iiSDKCore`의 Contracts 설치본을 사용하였다.
- 독립 소비자는 Qt·Threads·파일 공급자 탐색을 비활성화하였다. 소비자 바이너리의 동적 의존성은 시스템 C++ 런타임과 시스템 라이브러리뿐이다.

현재 변경의 실행 검증 범위는 macOS arm64이다. Windows·Linux·WASM의 이번 스키마 실행 검증은 수행하지 않았다. 기존 Windows 문서의 2/2 기록은 2026-10-20 스냅샷의 결과이다. `~/.local/SDK/iiJson` 실사용 설치본은 교체하지 않았다.

상세 로그는 `build/domain-schema-release-install.log`, sanitizer 로그는 `build/schema-sanitizers-tests.log`에 있다. JUnit과 소스·스테이징 파일 검증 결과는 `build/domain-schema-verification.json`에 함께 기록한다. macOS CMake는 정적 라이브러리를 설치한 뒤 `ranlib`를 실행하므로 archive 전체의 해시 대신 실제 오브젝트 멤버의 해시를 대조한다.
