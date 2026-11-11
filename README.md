# Auto Layout (Geode 모드, 지메 2.2081 / Geode v5.11.2)

에디터에서 빈 맵을 직접 플레이하면, 내가 지나간 경로 주변에 블록·가시·포탈이 자동으로 깔리는 모드.

## 사용법
1. 에디터에서 빈 맵 열기 → 화면 위쪽 `GEN: OFF` 버튼을 눌러 `GEN: ON`
2. 플레이테스트 시작 → 앞쪽에 게임모드/속도 포탈이 랜덤으로 깔림 → 마음대로 플레이
3. 플레이테스트 정지 → 지나간 경로에 맞춰 장애물 생성 (GEN 은 자동으로 OFF)
4. 다시 플레이테스트 = 결과물 테스트. 새로 만들려면 `GEN: ON` 후 다시 플레이 (이전 결과는 지워짐)

## 설정 (지오드 메뉴 → Auto Layout → 톱니)
- Mode: 사용할 게임모드 (여러 개 선택 가능)
- Frame perfects: 큐브/로봇 점프 중 몇 개를 프레임퍼펙으로 만들지
- Frame perfect FPS: 60 / 120 / 240
- Difficulty, Obstacle density, Length, 속도/미니 포탈

## 빌드
레포에 이 폴더 내용을 올리면 Actions 가 `.geode` 를 만들어 줌 (Artifacts → AutoLayout-geode).
