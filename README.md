# Image Background (Geode 모드, 지메 2.2081 / Geode v5.11.2)

사진을 에디터 오브젝트(색칠된 타원 붓질)로 다시 그려서 맵 배경으로 쓰는 모드. 이미지를 넣는 게 아니라 오브젝트로 그림.

## 사용법
1. 에디터 화면 위쪽 `IMG Folder` 버튼 → 열린 폴더에 `.png` / `.jpg` 사진 넣기
2. `IMG to BG` 버튼 → 폴더에서 가장 최근 사진이 지금 보고 있는 화면 가운데에 오브젝트로 만들어짐
3. 만들어진 사각형은 전부 선택된 상태라 바로 옮기거나 크기를 바꿀 수 있음. 되돌리기(Undo)로 삭제

## 스타일 (설정의 Style)
- `painting` (기본): 겹쳐지는 타원 붓질로 그림을 다시 그림. 큰 붓질부터 깔고 점점 작은 붓질로 디테일을 채워서 진짜 그림처럼 보임
- `pixels`: 사각형 격자 (픽셀아트)

## 설정 (`IMG Settings`)
- Style: painting / pixels
- Painting: strokes — 붓질 수 = 오브젝트 수. 많을수록 세밀함 (기본 800, 2000이면 아주 세밀)
- Painting: width in blocks — 완성된 그림의 가로 길이 (기본 20블록)
- Pixels: width / cell size / colour detail — 픽셀 스타일 전용
- Group ID: 모든 오브젝트에 넣을 그룹 (0 = 없음). Move 트리거 하나로 그림 전체를 움직일 때 사용
- Editor layer: 오브젝트를 넣을 에디터 레이어 (숨기기·잠그기용)

## 카메라를 따라오는 진짜 배경처럼 만들기
Group ID 를 정해서 만든 뒤, Move 트리거를 놓고 Target Group 에 그 그룹, `Lock to Player X` 를 켜면 그림이 화면을 따라옴.

## 빌드
레포에 이 폴더 내용을 올리면 Actions 가 `.geode` 를 만들어 줌 (Artifacts → ImageBG-geode).
