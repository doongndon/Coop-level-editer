// mbedTLS 기본 설정을 읽은 뒤에 추가로 적용되는 설정.
//
// 윈도우 빌드는 clang이 MSVC 방식으로 컴파일하는데, 이때 mbedTLS의
// 하드웨어 AES 가속 코드(aesni.c)가 "aes 기능 없이 컴파일되는 함수에
// always_inline 함수를 넣을 수 없다"며 컴파일에 실패한다.
//
// 이 모드는 짧은 JSON 메시지 몇 개를 주고받을 뿐이라 AES 가속이 필요 없다.
// 끄더라도 같은 알고리즘을 소프트웨어로 처리할 뿐 안전성은 달라지지 않는다.
#undef MBEDTLS_AESNI_C
