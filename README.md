# Auto Layout (Geode 모드, 지메 2.2081 / Geode v5.11.2)

에디터에서 직접 플레이하면, 플레이하는 동안 내 주변에 맵이 만들어지는 모드.

## 사용법
1. 에디터에서 맵 열기 → 화면 위쪽 `GEN: OFF` 버튼을 눌러 `GEN: ON` (옆의 `Settings` = 설정 창)
2. 플레이테스트 시작 → 그냥 플레이
   - 공중에서 클릭하면 그 자리에 공점이 생겨서 바로 공점 점프가 됨 (큐브·로봇·볼)
   - 몇 초마다 내 앞에 게임모드/속도/미니 포탈이 생김
   - 지나온 자리 바로 뒤에 블록·가시가 생김
3. 플레이테스트 정지 → 포탈 위아래를 벽으로 막음 (포탈을 피해 갈 수 없게)
4. `GEN` 은 직접 끌 때까지 ON. 켠 채로 다시 플레이테스트하면 이전 생성물(에디터 레이어 77)을 지우고 새로 만듦. 결과물 테스트는 `GEN: OFF` 로

## 설정
- Build while playing: 끄면 블록·가시는 정지할 때 한꺼번에 생성 (공점·포탈은 항상 실시간)
- Mode: 사용할 게임모드 (여러 개 선택 가능)
- Frame perfects / Frame perfect FPS: 큐브·로봇 점프 중 프레임퍼펙 개수, 60 / 120 / 240
- Difficulty, Obstacle density, Jump orbs, 속도/미니 포탈

## 빌드
레포에 이 폴더 내용을 올리면 Actions 가 `.geode` 를 만들어 줌 (Artifacts → AutoLayout-geode).

플레이 중에 플레이어 위치에 오브젝트를 놓는 방식은 ProfDragon 의 Layout Generator 모드를 참고함.
