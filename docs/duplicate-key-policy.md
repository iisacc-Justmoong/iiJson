# JSON 중복 키 호환 정책

일반 `parse(text, limits)`와 `Codec<T>`·스키마 텍스트 파싱은 이전처럼 디코딩된
객체 키가 반복되면 `duplicate_key`를 반환한다. 기존 호출의 기본 동작과
`parse(text, {})` 문법은 바뀌지 않는다.

과거 파일 형식이 중복 키의 마지막 값을 사용하도록 정의한 경우에만
`parse_with_duplicate_keys(text, DuplicateKeyPolicy::last_wins, limits)`를 호출한다.
각 중첩 객체에서 이스케이프를 디코딩한 뒤 같은 키를 비교하며 마지막 값을
사용한다. `DuplicateKeyPolicy::reject`는 기본 파서와 같은 오류와 위치를 준다.
정의되지 않은 정책 값은 `unsupported_value`로 거부한다.

중복된 모든 값은 실제로 파싱·검증한다. 문법, UTF-8, 숫자, 입력 바이트,
문자열, 항목 수와 깊이 제한을 우회하지 않으며 실패 시 부분 값을 반환하지
않는다. 힙 할당 실패는 기존 파서처럼 예외이다. 중복 호환은 해당 입력에만
적용되고 전역 설정을 변경하지 않는다. 스키마가 필요하면 반환된 값에
`validate(value, schema, limits)`를 명시적으로 호출한다.

[실행 가능한 예제](../example/legacy-duplicate-policy.cpp)와
`tests/duplicate_policy.cpp`는 기본 엄격성, 중첩·이스케이프 키, 마지막 값,
문법·자원 제한과 알 수 없는 정책을 검사한다. 이 API 자체가 다른 모듈의
레거시 문서 읽기 경로에 채택되었다는 뜻은 아니다.
