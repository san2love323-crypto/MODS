# -*- coding: utf-8 -*-
"""
게임 로직 모듈 (game_logic.py)
------------------------------------------------
이 파일은 server.py 가 실행 중인 상태에서도 통째로 교체 + 자동 리로드가 가능합니다.
(server.py 는 소켓/HTTP 뼈대만 담당하고, 실제 게임 규칙/DB 처리는 전부 여기 있음)

업데이트 방법:
  1) uploader.html 을 열어서 새 game_logic.py 를 업로드
     -> 서버가 문법 검사 후 자동으로 importlib.reload() 실행 (서버 재시작 없음)
  2) 또는 이 파일을 직접 같은 폴더에 덮어쓰기만 해도
     server.py 의 자동 감지 스레드가 몇 초 안에 알아서 리로드함
"""
import json, os, sys, time, threading, sqlite3, hashlib, gzip as _gzip
from contextlib import contextmanager

# 한국어 Windows(cp949) 콘솔에서 이모지 출력 시 크래시 방지
try:
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='ignore')
    if hasattr(sys.stderr, 'reconfigure'):
        sys.stderr.reconfigure(encoding='utf-8', errors='ignore')
except Exception:
    pass


def _p(*args):
    try:
        print(*args)
    except Exception:
        try:
            enc = getattr(sys.stdout, 'encoding', None) or 'utf-8'
            sys.stdout.write(' '.join(str(a) for a in args).encode(enc, 'ignore').decode(enc, 'ignore') + '\n')
        except Exception:
            pass


PORT = int(os.environ.get('RIFT_PORT', 8777))
HOST = os.environ.get('RIFT_HOST', '0.0.0.0')

# 🎯 관리자(개발자) 비밀번호 — 클라이언트 ADMIN_PASSWORD 와 반드시 동일해야 함
ADMIN_PASSWORD = os.environ.get('RIFT_ADMIN_PASS') or ('dajkfbqsjhfgeqkfejkfhweiuuqnsiqjsidwidj23891381983293898329328304928')
# 관리자 닉네임 — 클라이언트 ADMIN_NICK 과 동일해야 함 (구매 알림 우편 수신자)
ADMIN_NICK = os.environ.get('RIFT_ADMIN_NICK') or '개발자'
DB_PATH = os.environ.get('RIFT_DB') or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), 'rift_server.db')
_lock = threading.Lock()

# 이 API 경로 목록에 없는 GET 요청은 정적 파일(게임 index.html 등)로 서빙한다.
API_PATHS = {
    '/chat/send', '/chat/list', '/chat/poll', '/rank/submit', '/rank/list', '/online',
    '/guild/create', '/guild/join', '/guild/leave', '/guild/list', '/guild/info', '/boss/state', '/boss/hit',
    '/market/sell', '/market/list', '/market/buy',
    '/auction/create', '/auction/list', '/auction/bid', '/auction/buyout',
    '/auction/mine', '/auction/cancel',
    '/nick/claim',
    '/mail/list', '/mail/take', '/mail/send', '/ping',
    '/notice/get', '/notice/set',
    '/click/report', '/ban/check',
    '/admin/cmd', '/admin/pending', '/admin/ban', '/admin/unban',
    '/admin/rank/set', '/admin/rank/list', '/admin/rank/get',
    '/purchase/request', '/purchase/list', '/purchase/approve', '/purchase/reject', '/purchase/mine',
}

# index.html 등 정적 파일을 찾을 폴더 (기본: server.py 와 같은 폴더)
STATIC_DIR = os.environ.get('RIFT_STATIC_DIR') or os.path.dirname(os.path.abspath(__file__))

# 🔗 SEO: 서버가 내보내는 모든 HTML 페이지의 <head>에 자동 삽입되는 canonical 태그
#    (검색엔진에 공식 주소를 알려줌 — index.html 파일을 직접 안 고쳐도 항상 적용됨)
CANONICAL_URL = os.environ.get('RIFT_CANONICAL') or 'https://manmulsang.kro.kr/'
CANONICAL_TAG = ('<link rel="canonical" href="%s" />' % CANONICAL_URL).encode('utf-8')

# 📴 오프라인 지원: 게임을 브라우저에 캐시해 서버가 꺼져도 접속·플레이 가능하게 하는
#    서비스워커 코드 (파일 없이 서버가 직접 생성해서 /sw.js 로 제공)
SW_JS = """
const CACHE='rift-offline-v1';
const CORE=['/','/index.html','/favicon.ico'];
self.addEventListener('install',e=>{
  e.waitUntil(caches.open(CACHE).then(c=>c.addAll(CORE).catch(()=>{})).then(()=>self.skipWaiting()));
});
self.addEventListener('activate',e=>{ e.waitUntil(self.clients.claim()); });
self.addEventListener('fetch',e=>{
  const u=new URL(e.request.url);
  if(e.request.method!=='GET') return;
  // API 요청은 네트워크만 (실패 시 게임이 자체적으로 오프라인 모드 처리)
  if(u.pathname.startsWith('/chat')||u.pathname.startsWith('/rank')||u.pathname.startsWith('/guild')
   ||u.pathname.startsWith('/boss')||u.pathname.startsWith('/mail')||u.pathname.startsWith('/admin')
   ||u.pathname.startsWith('/auction')||u.pathname.startsWith('/market')||u.pathname.startsWith('/purchase')
   ||u.pathname.startsWith('/nick')||u.pathname.startsWith('/click')||u.pathname.startsWith('/ban')
   ||u.pathname.startsWith('/online')||u.pathname.startsWith('/ping')) return;
  // 정적 파일: 네트워크 우선, 실패하면(서버 꺼짐) 캐시로 → 오프라인 플레이
  e.respondWith(
    fetch(e.request).then(res=>{
      try{ const cp=res.clone(); caches.open(CACHE).then(c=>c.put(e.request,cp)); }catch(err){}
      return res;
    }).catch(()=>caches.match(e.request).then(m=>m||caches.match('/index.html')))
  );
});
""".encode("utf-8")

_MIME = {
    '.html': 'text/html; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.ico': 'image/x-icon',
    # 👇 아래 두 항목이 들어있는지 확인 후 없으면 추가해 주세요!
    '.xml': 'application/xml; charset=utf-8',
    '.txt': 'text/plain; charset=utf-8',
}

# ---- 요청 속도 제한 (도배/스팸 방지) ----
# 게임은 채팅/랭킹/길드/보스/오토클리커 리포트/관리자 폴링 등 주기적 요청이 많고,
# 같은 공유 IP(PC방·가정 공유기)에서 여러 명이 접속할 수 있으므로 넉넉하게 잡는다.
RATE_LIMIT = int(os.environ.get('RIFT_RATE', 200))   # IP당 10초에 허용 요청 수
_rate = {}
_rate_lock = threading.Lock()

# 이 경로들은 게임 동작에 필수인 폴링/조회라 속도 제한에서 제외한다
# (도배 위험이 낮은 읽기성 요청 · 게임이 자동으로 보내는 요청)
RATE_EXEMPT = {
    '/ping', '/ban/check', '/click/report', '/admin/pending', '/admin/rank/get',
    '/purchase/mine', '/purchase/list',
    '/chat/list', '/chat/poll', '/rank/list', '/online', '/guild/list', '/guild/info',
    '/boss/state', '/mail/list', '/auction/list', '/auction/mine', '/notice/get',
}

def rate_ok(ip, path=None):
    if RATE_LIMIT <= 0:
        return True
    if path and path in RATE_EXEMPT:
        return True
    now_t = time.time()
    with _rate_lock:
        q = _rate.setdefault(ip, [])
        while q and now_t - q[0] > 10:
            q.pop(0)
        if len(q) >= RATE_LIMIT:
            return False
        q.append(now_t)
        if len(_rate) > 5000:
            for k in [k for k, v in _rate.items() if not v or now_t - v[-1] > 60]:
                _rate.pop(k, None)
        return True


def db():
    conn = sqlite3.connect(DB_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    # ⚡ 성능 튜닝: WAL 모드 + 동기화 완화 → 쓰기 속도 대폭 향상
    try:
        conn.execute('PRAGMA journal_mode=WAL')
        conn.execute('PRAGMA synchronous=NORMAL')
        conn.execute('PRAGMA cache_size=-8000')
        conn.execute('PRAGMA temp_store=MEMORY')
    except Exception:
        pass
    return conn


# ⚡ 정적 파일 메모리 캐시: 디스크를 매번 읽지 않고 RAM에서 즉시 응답.
#    gzip 압축본도 함께 캐시 → 670KB짜리 index.html이 ~150KB로 전송되어 로딩 급가속.
import gzip as _gzip
_static_cache = {}          # fpath -> {'mtime':, 'raw':, 'gz':}
_static_lock = threading.Lock()

def get_static_cached(fpath):
    try:
        mtime = os.path.getmtime(fpath)
    except Exception:
        return None
    with _static_lock:
        ent = _static_cache.get(fpath)
        if ent and ent['mtime'] == mtime:
            return ent
    try:
        with open(fpath, 'rb') as f:
            raw = f.read()
    except Exception:
        return None
    gz = None
    ext = os.path.splitext(fpath)[1].lower()
    if ext in ('.html', '.js', '.css', '.json', '.xml', '.txt') and len(raw) > 1024:
        try:
            gz = _gzip.compress(raw, 6)
        except Exception:
            gz = None
    ent = {'mtime': mtime, 'raw': raw, 'gz': gz}
    with _static_lock:
        _static_cache[fpath] = ent
        if len(_static_cache) > 64:
            _static_cache.pop(next(iter(_static_cache)), None)
    return ent


@contextmanager
def db_ctx():
    """sqlite3.Connection을 'with'로 쓰면 커밋/롤백만 하고 닫지는 않으므로,
    요청마다 연결이 계속 쌓여 누적되다 결국 서버가 죽는 문제(자원 고갈)를 막기 위해
    항상 close()까지 보장하는 래퍼."""
    conn = db()
    try:
        with conn:
            yield conn
    finally:
        conn.close()


def init_db():
    with db() as c:
        c.executescript('''
        CREATE TABLE IF NOT EXISTS chat(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            room TEXT NOT NULL DEFAULT 'global',
            nick TEXT NOT NULL, msg TEXT NOT NULL, ts INTEGER NOT NULL);
        CREATE INDEX IF NOT EXISTS idx_chat ON chat(room, id);

        CREATE TABLE IF NOT EXISTS ranking(
            nick TEXT PRIMARY KEY, power REAL DEFAULT 0, stage INTEGER DEFAULT 1,
            level INTEGER DEFAULT 1, tier TEXT DEFAULT '', ts INTEGER NOT NULL);

        CREATE TABLE IF NOT EXISTS players(
            nick TEXT PRIMARY KEY, last_seen INTEGER NOT NULL,
            stage INTEGER DEFAULT 1, level INTEGER DEFAULT 1, guild TEXT DEFAULT '');

        CREATE TABLE IF NOT EXISTS guilds(
            name TEXT PRIMARY KEY, owner TEXT NOT NULL, notice TEXT DEFAULT '',
            score REAL DEFAULT 0, raid_score REAL DEFAULT 0, ts INTEGER NOT NULL);

        CREATE TABLE IF NOT EXISTS guild_members(
            guild TEXT NOT NULL, nick TEXT NOT NULL, ts INTEGER NOT NULL,
            PRIMARY KEY(guild, nick));

        CREATE TABLE IF NOT EXISTS worldboss(
            id INTEGER PRIMARY KEY CHECK(id=1), name TEXT, hp REAL, max_hp REAL,
            season INTEGER DEFAULT 1, ts INTEGER);

        CREATE TABLE IF NOT EXISTS boss_damage(
            season INTEGER NOT NULL, nick TEXT NOT NULL, dmg REAL DEFAULT 0,
            PRIMARY KEY(season, nick));

        CREATE TABLE IF NOT EXISTS market(
            id INTEGER PRIMARY KEY AUTOINCREMENT, seller TEXT NOT NULL,
            item TEXT NOT NULL, grade TEXT, price INTEGER NOT NULL,
            item_json TEXT DEFAULT '',
            sold INTEGER DEFAULT 0, buyer TEXT DEFAULT '', ts INTEGER NOT NULL);

        -- 경매 시스템
        CREATE TABLE IF NOT EXISTS auction(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            seller TEXT NOT NULL,
            item TEXT NOT NULL,
            grade TEXT DEFAULT '',
            item_json TEXT DEFAULT '',      -- 장비 원본(구매자에게 그대로 전달)
            start_price INTEGER NOT NULL,   -- 시작가
            buyout INTEGER DEFAULT 0,       -- 즉시구매가 (0이면 없음)
            cur_bid INTEGER DEFAULT 0,      -- 현재 최고 입찰가
            cur_bidder TEXT DEFAULT '',     -- 현재 최고 입찰자
            is_private INTEGER DEFAULT 0,   -- 1이면 비공개(코드 필요)
            code TEXT DEFAULT '',           -- 비공개 입장 코드
            end_ts INTEGER NOT NULL,        -- 종료 시각
            closed INTEGER DEFAULT 0,       -- 1이면 정산 완료
            ts INTEGER NOT NULL);
        CREATE INDEX IF NOT EXISTS idx_auc ON auction(closed, end_ts);

        CREATE TABLE IF NOT EXISTS auction_bids(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            auc_id INTEGER NOT NULL, nick TEXT NOT NULL,
            amount INTEGER NOT NULL, ts INTEGER NOT NULL);

        CREATE TABLE IF NOT EXISTS mail(
            id INTEGER PRIMARY KEY AUTOINCREMENT, receiver TEXT NOT NULL,
            sender TEXT NOT NULL, subject TEXT, body TEXT,
            gold INTEGER DEFAULT 0, item_json TEXT DEFAULT '',
            taken INTEGER DEFAULT 0, ts INTEGER NOT NULL);
        CREATE INDEX IF NOT EXISTS idx_mail ON mail(receiver, taken);

        -- 닉네임 소유권 (한 번 쓰면 그 기기(토큰)만 계속 사용 가능, 다른 사람은 사용 불가)
        CREATE TABLE IF NOT EXISTS nick_owner(
            nick TEXT PRIMARY KEY, token TEXT NOT NULL, ts INTEGER NOT NULL);

        -- 🚫 정지(밴) 기록: 오토클리커 감지 시 여기에 기록
        CREATE TABLE IF NOT EXISTS bans(
            nick TEXT PRIMARY KEY,
            until_ts INTEGER NOT NULL,      -- 이 시각(unix)까지 정지
            reason TEXT DEFAULT '',
            interval_ms REAL DEFAULT 0,     -- 감지된 최소 클릭 간격(ms)
            ts INTEGER NOT NULL);           -- 정지 부과 시각

        -- 🖱️ 오토클리커 감지용 최근 클릭 타임스탬프 로그(밀리초)
        CREATE TABLE IF NOT EXISTS click_log(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            nick TEXT NOT NULL,
            t_ms INTEGER NOT NULL);          -- 클릭 시각(ms 단위 unix)
        CREATE INDEX IF NOT EXISTS idx_click ON click_log(nick, id);

        -- 🎯 관리자 원격 명령 (대상 유저가 접속하면 적용)
        CREATE TABLE IF NOT EXISTS admin_cmd(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            target TEXT NOT NULL,            -- 대상 유저 닉네임
            field TEXT NOT NULL,             -- gold|stat|stage|level|job
            value TEXT NOT NULL,
            applied INTEGER DEFAULT 0,       -- 1이면 적용 완료
            admin TEXT DEFAULT '',
            ts INTEGER NOT NULL);
        CREATE INDEX IF NOT EXISTS idx_admincmd ON admin_cmd(target, applied);

        -- 💳 구매 요청 (유저가 계좌이체 후 구매 문의 → 관리자가 승인하면 지급)
        CREATE TABLE IF NOT EXISTS purchase_req(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            nick TEXT NOT NULL,              -- 요청한 유저
            product TEXT NOT NULL,           -- 상품 ID
            product_name TEXT DEFAULT '',    -- 상품 표시명
            depositor TEXT DEFAULT '',       -- 입금자명(유저가 입력)
            status TEXT DEFAULT 'pending',   -- pending|approved|rejected
            ts INTEGER NOT NULL);
        CREATE INDEX IF NOT EXISTS idx_preq ON purchase_req(status, ts);

        -- 서버 메타 정보 (채팅 일일 초기화 마커 등)
        CREATE TABLE IF NOT EXISTS meta(
            k TEXT PRIMARY KEY, v TEXT NOT NULL);

        -- 👑 개발자 등급 (닉네임 -> 등급 숫자)
        CREATE TABLE IF NOT EXISTS admin_ranks(
            nick TEXT PRIMARY KEY,
            rank INTEGER NOT NULL DEFAULT 1,   -- 1신입 2인턴 3만물상감시자 4차원감독관 5균열관리자 6부개발자 7개발자
            granted_by TEXT DEFAULT '',
            ts INTEGER NOT NULL);
        ''')
        # 기존 DB 호환: 없는 컬럼 자동 추가
        for tbl, col, decl in [('mail', 'item_json', "TEXT DEFAULT ''"),
                               ('market', 'item_json', "TEXT DEFAULT ''"),
                               ('guilds', 'raid_score', "REAL DEFAULT 0")]:
            cols = [r[1] for r in c.execute('PRAGMA table_info(%s)' % tbl).fetchall()]
            if col not in cols:
                c.execute('ALTER TABLE %s ADD COLUMN %s %s' % (tbl, col, decl))

        row = c.execute('SELECT 1 FROM worldboss WHERE id=1').fetchone()
        if not row:
            c.execute('INSERT INTO worldboss(id,name,hp,max_hp,season,ts) VALUES(1,?,?,?,1,?)',
                      ('차원의 포식자', 1e12, 1e12, int(time.time())))


def now(): return int(time.time())
def now_ms(): return int(time.time() * 1000)


# ============================================================================
# 👑 개발자 등급 시스템
# ----------------------------------------------------------------------------
#  7 개발자        : 전부 가능 + 승급/강등 + 강제 이름 변경 (ADMIN_NICK 은 항상 7)
#  6 부개발자      : 전부 가능 (승급·이름변경 제외)
#  5 균열 관리자   : 유저 원격 조작 전체(직업 포함) + 상품 지급/회수 + 밴 + 구매
#  4 차원 감독관   : 유저 원격 조작(골드·스탯·스테이지·레벨) + 밴 + 구매
#  3 만물상 감시자 : 유저 원격 골드 지급 + 밴 + 구매
#  2 인턴          : 밴 + 현질(구매) 승인
#  1 신입          : 밴만 가능
#  0 일반 유저     : 콘솔 사용 불가
# ============================================================================
RANK_NAMES = {7:'개발자', 6:'부개발자', 5:'균열 관리자', 4:'차원 감독관',
              3:'만물상 감시자', 2:'인턴', 1:'신입', 0:'일반'}

def rank_of(c, nick):
    if not nick:
        return 0
    if nick == ADMIN_NICK:
        return 7
    row = c.execute('SELECT rank FROM admin_ranks WHERE nick=?', (nick,)).fetchone()
    return int(row['rank']) if row else 0

# ============================================================================
# 🐲 레이드 시스템 (기존 월드보스를 시간제 레이드로 개편)
#   - 매시 정각부터 10분간 개방 (예: 13:00~13:10, 14:00~14:10 ...)
#   - 참가자(닉네임+직업)를 추적해 모두에게 보여줌 → 함께 공격하는 화면 구성
# ============================================================================
# ============================================================================
# ⚡ 채팅 즉시 전달 (롱폴링)
#   - 클라이언트가 /chat/poll 로 대기 → 새 메시지가 오는 '순간' 즉시 응답
#   - 폴링 주기 없이 실시간에 가까운 채팅 반영 (평균 지연 ~0.1초)
# ============================================================================
_chat_cond = threading.Condition()
_chat_seq = {}     # room -> 마지막 메시지 id (메모리 캐시)

def chat_seq(room):
    with _chat_cond:
        if room in _chat_seq:
            return _chat_seq[room]
    # 최초 조회는 DB에서
    with _lock, db_ctx() as c:
        row = c.execute('SELECT IFNULL(MAX(id),0) m FROM chat WHERE room=?', (room,)).fetchone()
        seq = row['m'] if row else 0
    with _chat_cond:
        _chat_seq[room] = max(_chat_seq.get(room, 0), seq)
        return _chat_seq[room]

def chat_notify(room, new_id):
    with _chat_cond:
        _chat_seq[room] = max(_chat_seq.get(room, 0), new_id)
        _chat_cond.notify_all()

def kst_day():
    """한국(KST, UTC+9) 기준 오늘 날짜 문자열"""
    return time.strftime('%Y-%m-%d', time.gmtime(now() + 9 * 3600))

def ensure_chat_daily_reset(c):
    """🧹 한국시간 자정 기준으로 하루가 바뀌면 채팅 전체 비우기"""
    today = kst_day()
    row = c.execute("SELECT v FROM meta WHERE k='chat_reset_day'").fetchone()
    if row and row['v'] == today:
        return
    c.execute('DELETE FROM chat')
    c.execute("INSERT INTO meta(k,v) VALUES('chat_reset_day',?) "
              "ON CONFLICT(k) DO UPDATE SET v=?", (today, today))
    if not row:
        return   # 최초 기동 시엔 마커만 기록
    # 초기화 안내 메시지 1건 남기기
    c.execute("INSERT INTO chat(room,nick,msg,ts) VALUES('global','시스템',"
              "'🧹 매일 자정(한국시간) 채팅이 초기화됩니다. 오늘도 좋은 하루!', ?)", (now(),))
    try:
        nid = c.execute('SELECT last_insert_rowid() i').fetchone()['i']
        chat_notify('global', nid)
    except Exception:
        pass


def chat_poll(room, since, wait_sec):
    """since 이후 새 메시지가 생길 때까지 최대 wait_sec 대기 (락을 잡지 않고 대기)."""
    wait_sec = max(1, min(12, int(wait_sec or 10)))
    deadline = time.time() + wait_sec
    while True:
        cur = chat_seq(room)
        if cur > since:
            with _lock, db_ctx() as c:
                rows = c.execute('SELECT id,nick,msg,ts FROM chat WHERE room=? AND id>? '
                                 'ORDER BY id ASC LIMIT 60', (room, since)).fetchall()
            return {'ok': True, 'messages': [dict(r) for r in rows], 'last': cur}
        remain = deadline - time.time()
        if remain <= 0:
            return {'ok': True, 'messages': [], 'last': cur}
        with _chat_cond:
            _chat_cond.wait(min(remain, 5))


RAID_OPEN_SEC = 600          # 매시 개방 시간(초) = 10분
_raid_part = {}              # nick -> {'job':, 'ts':, 'dmg':}
_raid_lock = threading.Lock()

def raid_window():
    t = now() % 3600
    if t < RAID_OPEN_SEC:
        return {'open': True, 'close_in': RAID_OPEN_SEC - t, 'next_in': 0}
    return {'open': False, 'close_in': 0, 'next_in': 3600 - t}

def raid_touch(nick, job, dmg):
    with _raid_lock:
        e = _raid_part.get(nick) or {'job': job, 'ts': 0, 'dmg': 0}
        e['job'] = job or e.get('job') or 'shopkeeper'
        e['ts'] = now(); e['dmg'] = dmg
        _raid_part[nick] = e
        # 90초 이상 조용한 참가자 정리
        cut = now() - 90
        for k in [k for k, v in _raid_part.items() if v['ts'] < cut]:
            _raid_part.pop(k, None)

def raid_participants():
    with _raid_lock:
        cut = now() - 90
        out = [{'nick': k, 'job': v['job'], 'dmg': v['dmg'], 'ts': v['ts']}
               for k, v in _raid_part.items() if v['ts'] >= cut]
    out.sort(key=lambda x: -x['ts'])
    return out[:12]


def cmd_min_rank(field):
    """admin_cmd 필드별 필요 최소 등급"""
    return {'gold': 3, 'stat': 4, 'stage': 4, 'level': 4,
            'job': 5, 'grant_product': 5, 'revoke_product': 5,
            'rename': 7}.get(field, 6)


# ============================================================================
# 🏰 길드 레이드 / 버프
# ----------------------------------------------------------------------------
# 길드원이 월드보스에 준 피해가 guilds.raid_score 로 누적된다.
# 누적 점수에 따라 길드 레벨이 오르고, 레벨에 비례해 길드원 전체가
# 공격력 버프(%)를 받는다. (레벨당 +3%, 최대 레벨 30 = +90%)
# ============================================================================
GUILD_MAX_LEVEL = 30
GUILD_ATK_PER_LEVEL = 0.03      # 레벨당 공격력 +3%

def guild_level_from_score(score):
    """누적 레이드 점수 -> 길드 레벨. 레벨이 오를수록 필요 점수가 커진다."""
    score = max(0.0, float(score or 0))
    lvl = 0
    need = 1e7
    total = 0.0
    while lvl < GUILD_MAX_LEVEL and score >= total + need:
        total += need
        need *= 1.6
        lvl += 1
    return lvl

def guild_next_need(score):
    """다음 레벨까지 남은 점수(표시용)."""
    score = max(0.0, float(score or 0))
    lvl = 0
    need = 1e7
    total = 0.0
    while lvl < GUILD_MAX_LEVEL and score >= total + need:
        total += need
        need *= 1.6
        lvl += 1
    if lvl >= GUILD_MAX_LEVEL:
        return {'level': lvl, 'cur': score - total, 'need': 0, 'max': True}
    return {'level': lvl, 'cur': score - total, 'need': need, 'max': False}

def guild_atk_buff(level):
    """길드 레벨 -> 공격력 버프 배수 (1.0 = 버프없음)."""
    return 1.0 + GUILD_ATK_PER_LEVEL * min(GUILD_MAX_LEVEL, max(0, int(level)))

def guild_of(c, nick):
    """해당 유저가 속한 길드 이름 (없으면 '')."""
    if not nick:
        return ''
    row = c.execute('SELECT guild FROM guild_members WHERE nick=? LIMIT 1', (nick,)).fetchone()
    return row['guild'] if row else ''




# ============================================================================
# 🚫 오토클리커 감지 시스템
# ----------------------------------------------------------------------------
# 요구사항:
#   - 클릭 간격이 0.01초(10ms) ~ 0.00000000000000000000000001초(1e-26초) 사이면 정지
#   - 10ms  → 10분(600초) 정지
#   - 1e-26초 → 240시간(864000초) 정지
#   - 그 사이는 로그 스케일로 비례해서 정지 시간 부과
#   - ⚠️ 오탐(誤探) 절대 금지: 사람이 물리적으로 불가능한 속도가
#     "연속으로 여러 번" 확인될 때만 정지. 느린 클릭·정상 클릭은 절대 정지 안 함.
#
# 판정 원리:
#   브라우저에서 실제 사람의 최고 연타는 대략 초당 10~15회(간격 ~65ms 이상).
#   물리적으로도 마우스/화면 이벤트는 보통 4ms(240Hz) 이하로는 못 내려간다.
#   따라서 "간격 10ms 이하"가 나오는 건 소프트웨어 자동클릭뿐이다.
#   여기서는 더 보수적으로 "간격 10ms 이하가 연속 CONSECUTIVE_HITS회 이상"일 때만
#   자동클릭으로 확정한다. (단 한 번의 우연한 짧은 간격으로는 절대 밴하지 않음)
# ============================================================================

AUTOCLICK_MAX_MS      = 10.0        # 이 간격(ms) 이하 = 의심 구간의 상한 (0.01초)
AUTOCLICK_MIN_MS      = 1e-23       # 하한 (0.00000000000000000000000001초 = 1e-26초 = 1e-23ms)
BAN_MIN_SEC           = 10 * 60     # 10분 (간격 10ms에 해당)
BAN_MAX_SEC           = 240 * 3600  # 240시간 (간격 1e-26초에 해당)
CONSECUTIVE_HITS      = 6           # 의심 간격이 "연속 6회" 이상 나와야 확정 (오탐 방지)
CLICK_WINDOW          = 12          # 최근 12개 클릭만 보관/분석


def ban_seconds_for_interval(interval_ms):
    """감지된 클릭 간격(ms)에 대응하는 정지 시간(초)을 로그 스케일로 계산.
    interval_ms 가 작을수록(빠를수록) 더 긴 정지.
      10ms   → 600초 (10분)
      1e-23ms(=1e-26초) → 864000초 (240시간)
    """
    iv = max(AUTOCLICK_MIN_MS, min(AUTOCLICK_MAX_MS, float(interval_ms)))
    import math
    # log10(iv) 를 [log10(MIN) .. log10(MAX)] 구간에서 0..1 로 정규화
    lo = math.log10(AUTOCLICK_MIN_MS)   # 가장 빠름(가장 강한 정지)
    hi = math.log10(AUTOCLICK_MAX_MS)   # 가장 느림(가장 약한 정지)
    frac = (hi - math.log10(iv)) / (hi - lo)   # iv=10ms→0, iv=1e-23ms→1
    frac = max(0.0, min(1.0, frac))
    sec = BAN_MIN_SEC + (BAN_MAX_SEC - BAN_MIN_SEC) * frac
    return int(round(sec))


def get_ban(c, nick):
    """현재 정지 중이면 (until_ts, reason, interval_ms) 반환, 아니면 None.
    만료된 정지는 자동으로 해제한다."""
    if not nick:
        return None
    row = c.execute('SELECT until_ts, reason, interval_ms FROM bans WHERE nick=?',
                    (nick,)).fetchone()
    if not row:
        return None
    if row['until_ts'] <= now():
        c.execute('DELETE FROM bans WHERE nick=?', (nick,))   # 만료 → 해제
        return None
    return {'until': row['until_ts'], 'reason': row['reason'],
            'interval_ms': row['interval_ms']}


def apply_ban(c, nick, interval_ms):
    """오토클리커로 확정된 유저에게 정지 부과 (기존보다 길면 갱신)."""
    sec = ban_seconds_for_interval(interval_ms)
    until = now() + sec
    existing = c.execute('SELECT until_ts FROM bans WHERE nick=?', (nick,)).fetchone()
    if existing and existing['until_ts'] >= until:
        until = existing['until_ts']   # 이미 더 센 정지가 있으면 유지
    c.execute('INSERT INTO bans(nick,until_ts,reason,interval_ms,ts) VALUES(?,?,?,?,?) '
              'ON CONFLICT(nick) DO UPDATE SET until_ts=?, reason=?, interval_ms=?, ts=?',
              (nick, until, 'autoclicker', interval_ms, now(),
               until, 'autoclicker', interval_ms, now()))
    # 클릭 로그는 정리
    c.execute('DELETE FROM click_log WHERE nick=?', (nick,))
    return {'banned': True, 'until': until, 'seconds': until - now(),
            'interval_ms': interval_ms}


def record_clicks_and_check(c, nick, timestamps_ms):
    """클라이언트가 보낸 클릭 시각(ms) 목록을 기록하고 오토클리커인지 판정.
    반환: 정지가 부과되면 밴 정보 dict, 아니면 None.

    확정 조건(오탐 방지): 최근 클릭들 중 '연속으로' CONSECUTIVE_HITS 개 이상의
    간격이 모두 AUTOCLICK_MAX_MS(10ms) 이하일 때만 자동클릭으로 확정한다.
    """
    if not nick or not timestamps_ms:
        return None
    # 정수 ms 로 저장
    for t in timestamps_ms:
        try:
            c.execute('INSERT INTO click_log(nick,t_ms) VALUES(?,?)', (nick, int(t)))
        except Exception:
            pass
    # 최근 CLICK_WINDOW 개만 남기고 오래된 것 삭제
    c.execute(
        'DELETE FROM click_log WHERE nick=? AND id NOT IN '
        '(SELECT id FROM click_log WHERE nick=? ORDER BY id DESC LIMIT ?)',
        (nick, nick, CLICK_WINDOW))
    rows = c.execute('SELECT t_ms FROM click_log WHERE nick=? ORDER BY id ASC',
                     (nick,)).fetchall()
    times = [r['t_ms'] for r in rows]
    if len(times) < CONSECUTIVE_HITS + 1:
        return None   # 표본 부족 → 절대 밴하지 않음

    # 인접 클릭 간격(ms) 계산
    intervals = [times[i+1] - times[i] for i in range(len(times)-1)]

    # "연속으로" 의심 간격이 이어지는 최대 길이와, 그 구간의 최소 간격을 찾는다
    run = 0
    best_run = 0
    run_min = None
    global_min_in_run = None
    for iv in intervals:
        # 음수(시계 역행)나 0은 조작 신호로 간주하되, 안전하게 0.0001ms로 처리
        if iv <= AUTOCLICK_MAX_MS:
            run += 1
            cur = iv if iv > 0 else 0.0001
            run_min = cur if run_min is None else min(run_min, cur)
            if run > best_run:
                best_run = run
                global_min_in_run = run_min
        else:
            run = 0
            run_min = None

    # 연속 의심 간격이 기준 이상일 때만 확정
    if best_run >= CONSECUTIVE_HITS and global_min_in_run is not None:
        return apply_ban(c, nick, global_min_in_run)
    return None



def close_auction(c, aid):
    """경매 1건 낙찰 처리: 판매자에겐 골드, 낙찰자에겐 아이템을 우편 발송"""
    row = c.execute('SELECT * FROM auction WHERE id=? AND closed=0', (aid,)).fetchone()
    if not row:
        return False
    c.execute('UPDATE auction SET closed=1 WHERE id=?', (aid,))
    if row['cur_bidder'] and row['cur_bid'] > 0:
        # 낙찰자 → 아이템
        c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,item_json,ts) '
                  'VALUES(?,?,?,?,0,?,?)',
                  (row['cur_bidder'], '경매장', '낙찰 상품',
                   '%s 낙찰 (%d G)' % (row['item'], row['cur_bid']),
                   row['item_json'] or '', now()))
        # 판매자 → 골드 (수수료 5%)
        fee = int(row['cur_bid'] * 0.05)
        c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) '
                  'VALUES(?,?,?,?,?,?)',
                  (row['seller'], '경매장', '판매 대금',
                   '%s 낙찰 (수수료 5%%: %d G)' % (row['item'], fee),
                   row['cur_bid'] - fee, now()))
    else:
        # 유찰 → 판매자에게 반환
        c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,item_json,ts) '
                  'VALUES(?,?,?,?,0,?,?)',
                  (row['seller'], '경매장', '유찰 반환',
                   '%s 입찰자가 없어 반환됩니다.' % row['item'],
                   row['item_json'] or '', now()))
    return True


def settle_auctions(c):
    """종료 시각이 지난 경매를 일괄 정산"""
    rows = c.execute('SELECT id FROM auction WHERE closed=0 AND end_ts<=?',
                     (now(),)).fetchall()
    for r in rows:
        close_auction(c, r['id'])
    return len(rows)


# ============================== API ==============================
def api(path, q, body):
    # GET 쿼리와 POST 본문을 통합 (한글 파라미터는 POST 본문 권장)
    def P(key, default=''):
        if key in body and body[key] is not None:
            return body[key]
        v = q.get(key)
        return v[0] if v else default

    with _lock, db_ctx() as c:
        # ---------- 1. 채팅 ----------
        if path == '/chat/send':
            ensure_chat_daily_reset(c)
            nick = (body.get('nick') or '익명')[:20]
            msg = (body.get('msg') or '')[:200]
            room = (body.get('room') or 'global')[:40]
            if not msg.strip():
                return {'ok': False, 'error': 'empty'}
            c.execute('INSERT INTO chat(room,nick,msg,ts) VALUES(?,?,?,?)',
                      (room, nick, msg, now()))
            new_id = c.execute('SELECT last_insert_rowid() i').fetchone()['i']
            c.execute('DELETE FROM chat WHERE room=? AND id NOT IN '
                      '(SELECT id FROM chat WHERE room=? ORDER BY id DESC LIMIT 200)', (room, room))
            chat_notify(room, new_id)   # ⚡ 대기 중인 유저들에게 즉시 전달
            return {'ok': True, 'id': new_id}

        if path == '/chat/list':
            ensure_chat_daily_reset(c)
            room = str(P('room', 'global'))[:40]
            since = int(P('since', 0) or 0)
            rows = c.execute('SELECT id,nick,msg,ts FROM chat WHERE room=? AND id>? '
                             'ORDER BY id DESC LIMIT 60', (room, since)).fetchall()
            return {'ok': True, 'messages': [dict(r) for r in reversed(rows)]}

        # ---------- 2. 랭킹 ----------
        if path == '/rank/submit':
            nick = (body.get('nick') or '익명')[:20]
            c.execute('INSERT INTO ranking(nick,power,stage,level,tier,ts) VALUES(?,?,?,?,?,?) '
                      'ON CONFLICT(nick) DO UPDATE SET power=excluded.power, stage=excluded.stage,'
                      'level=excluded.level, tier=excluded.tier, ts=excluded.ts',
                      (nick, float(body.get('power') or 0), int(body.get('stage') or 1),
                       int(body.get('level') or 1), (body.get('tier') or '')[:20], now()))
            c.execute('INSERT INTO players(nick,last_seen,stage,level,guild) VALUES(?,?,?,?,?) '
                      'ON CONFLICT(nick) DO UPDATE SET last_seen=excluded.last_seen,'
                      'stage=excluded.stage, level=excluded.level, guild=excluded.guild',
                      (nick, now(), int(body.get('stage') or 1), int(body.get('level') or 1),
                       (body.get('guild') or '')[:30]))
            return {'ok': True}

        if path == '/rank/list':
            rows = c.execute('SELECT nick,power,stage,level,tier FROM ranking '
                             'ORDER BY power DESC LIMIT 100').fetchall()
            return {'ok': True, 'rankings': [dict(r) for r in rows]}

        # ---------- 3. 접속자 ----------
        if path == '/online':
            cut = now() - 90
            rows = c.execute('SELECT nick,stage,level,guild FROM players WHERE last_seen>? '
                             'ORDER BY stage DESC LIMIT 50', (cut,)).fetchall()
            cnt = c.execute('SELECT COUNT(*) n FROM players WHERE last_seen>?', (cut,)).fetchone()['n']
            return {'ok': True, 'count': cnt, 'players': [dict(r) for r in rows]}

        # ---------- 4. 길드 ----------
        if path == '/guild/create':
            name = (body.get('name') or '').strip()[:30]
            owner = (body.get('nick') or '')[:20]
            if not name: return {'ok': False, 'error': 'no_name'}
            if c.execute('SELECT 1 FROM guilds WHERE name=?', (name,)).fetchone():
                return {'ok': False, 'error': 'exists'}
            c.execute('INSERT INTO guilds(name,owner,ts) VALUES(?,?,?)', (name, owner, now()))
            c.execute('INSERT OR REPLACE INTO guild_members(guild,nick,ts) VALUES(?,?,?)',
                      (name, owner, now()))
            return {'ok': True}

        if path == '/guild/join':
            name = (body.get('name') or '')[:30]
            nick = (body.get('nick') or '')[:20]
            if not c.execute('SELECT 1 FROM guilds WHERE name=?', (name,)).fetchone():
                return {'ok': False, 'error': 'not_found'}
            c.execute('DELETE FROM guild_members WHERE nick=?', (nick,))
            c.execute('INSERT OR REPLACE INTO guild_members(guild,nick,ts) VALUES(?,?,?)',
                      (name, nick, now()))
            c.execute('UPDATE players SET guild=? WHERE nick=?', (name, nick))
            return {'ok': True}

        if path == '/guild/leave':
            name = (body.get('name') or '')[:30]
            nick = (body.get('nick') or '')[:20]
            c.execute('DELETE FROM guild_members WHERE guild=? AND nick=?', (name, nick))
            c.execute('UPDATE players SET guild=? WHERE nick=?', ('', nick))
            # 길드장이 나가고 멤버가 없으면 길드 삭제, 있으면 다음 사람에게 위임
            owner_row = c.execute('SELECT owner FROM guilds WHERE name=?', (name,)).fetchone()
            if owner_row and owner_row['owner'] == nick:
                nxt = c.execute('SELECT nick FROM guild_members WHERE guild=? ORDER BY ts ASC LIMIT 1',
                                (name,)).fetchone()
                if nxt:
                    c.execute('UPDATE guilds SET owner=? WHERE name=?', (nxt['nick'], name))
                else:
                    c.execute('DELETE FROM guilds WHERE name=?', (name,))
            return {'ok': True}

        if path == '/guild/list':
            rows = c.execute(
                'SELECT g.name,g.owner,g.notice,g.raid_score,'
                '(SELECT COUNT(*) FROM guild_members m WHERE m.guild=g.name) members,'
                '(SELECT IFNULL(SUM(r.power),0) FROM guild_members m JOIN ranking r ON r.nick=m.nick '
                ' WHERE m.guild=g.name) score '
                'FROM guilds g ORDER BY g.raid_score DESC, score DESC LIMIT 50').fetchall()
            out = []
            for r in rows:
                d = dict(r)
                lvl = guild_level_from_score(d.get('raid_score') or 0)
                d['level'] = lvl
                d['atk_buff'] = guild_atk_buff(lvl)
                out.append(d)
            return {'ok': True, 'guilds': out}

        # 🏰 내 길드 상세 (레벨/버프/멤버/진행도)
        if path == '/guild/info':
            nick = str(P('nick', ''))[:20]
            name = str(P('name', ''))[:30] or guild_of(c, nick)
            if not name:
                return {'ok': True, 'in_guild': False}
            g = c.execute('SELECT name,owner,notice,IFNULL(raid_score,0) raid_score FROM guilds WHERE name=?',
                          (name,)).fetchone()
            if not g:
                return {'ok': True, 'in_guild': False}
            g = dict(g)
            prog = guild_next_need(g['raid_score'])
            lvl = prog['level']
            members = c.execute(
                'SELECT m.nick, IFNULL(r.power,0) power, IFNULL(p.stage,1) stage, IFNULL(p.level,1) level '
                'FROM guild_members m '
                'LEFT JOIN ranking r ON r.nick=m.nick '
                'LEFT JOIN players p ON p.nick=m.nick '
                'WHERE m.guild=? ORDER BY power DESC LIMIT 50', (name,)).fetchall()
            return {'ok': True, 'in_guild': True, 'name': g['name'], 'owner': g['owner'],
                    'notice': g['notice'], 'raid_score': g['raid_score'],
                    'level': lvl, 'max_level': GUILD_MAX_LEVEL,
                    'atk_buff': guild_atk_buff(lvl),
                    'progress': prog,
                    'members': [dict(m) for m in members]}

        # ---------- 5. 월드보스 ----------
        if path == '/boss/state':
            b = dict(c.execute('SELECT * FROM worldboss WHERE id=1').fetchone())
            top = c.execute('SELECT nick,dmg FROM boss_damage WHERE season=? '
                            'ORDER BY dmg DESC LIMIT 10', (b['season'],)).fetchall()
            rw = raid_window()
            return {'ok': True, 'boss': b, 'top': [dict(r) for r in top],
                    'raid': rw, 'participants': raid_participants() if rw['open'] else []}

        if path == '/boss/hit':
            rw = raid_window()
            if not rw['open']:
                return {'ok': False, 'error': 'raid_closed', 'next_in': rw['next_in']}
            nick = (body.get('nick') or '익명')[:20]
            dmg = max(0.0, float(body.get('dmg') or 0))
            raid_touch(nick, str(body.get('job') or '')[:24], dmg)
            b = c.execute('SELECT * FROM worldboss WHERE id=1').fetchone()
            hp = max(0.0, b['hp'] - dmg)
            season = b['season']
            c.execute('INSERT INTO boss_damage(season,nick,dmg) VALUES(?,?,?) '
                      'ON CONFLICT(season,nick) DO UPDATE SET dmg=dmg+?',
                      (season, nick, dmg, dmg))
            # 🏰 길드 레이드 점수 누적 (준 피해만큼)
            gname = guild_of(c, nick)
            guild_level = 0
            if gname and dmg > 0:
                c.execute('UPDATE guilds SET raid_score = IFNULL(raid_score,0) + ? WHERE name=?',
                          (dmg, gname))
                gs = c.execute('SELECT raid_score FROM guilds WHERE name=?', (gname,)).fetchone()
                if gs:
                    guild_level = guild_level_from_score(gs['raid_score'])
            killed = False
            if hp <= 0:
                killed = True
                season += 1
                new_hp = b['max_hp'] * 2.5
                c.execute('UPDATE worldboss SET hp=?,max_hp=?,season=?,ts=? WHERE id=1',
                          (new_hp, new_hp, season, now()))
                # 처치 보상 우편
                for r in c.execute('SELECT nick,dmg FROM boss_damage WHERE season=? '
                                   'ORDER BY dmg DESC LIMIT 20', (b['season'],)).fetchall():
                    c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) '
                              'VALUES(?,?,?,?,?,?)',
                              (r['nick'], '월드보스', '토벌 보상',
                               f"{b['name']} 처치 기여 보상", int(1e7), now()))
            else:
                c.execute('UPDATE worldboss SET hp=?,ts=? WHERE id=1', (hp, now()))
            return {'ok': True, 'hp': hp, 'killed': killed, 'season': season,
                    'guild': gname, 'guild_level': guild_level,
                    'guild_atk_buff': guild_atk_buff(guild_level)}

        # ---------- 6. 거래소 ----------
        if path == '/market/sell':
            c.execute('INSERT INTO market(seller,item,grade,price,item_json,ts) VALUES(?,?,?,?,?,?)',
                      ((body.get('nick') or '')[:20], (body.get('item') or '')[:40],
                       (body.get('grade') or '')[:20], int(body.get('price') or 0),
                       str(body.get('item_json') or '')[:2000], now()))
            return {'ok': True}

        if path == '/market/list':
            rows = c.execute('SELECT id,seller,item,grade,price FROM market '
                             'WHERE sold=0 ORDER BY id DESC LIMIT 50').fetchall()
            return {'ok': True, 'items': [dict(r) for r in rows]}

        if path == '/market/buy':
            iid = int(body.get('id') or 0)
            buyer = (body.get('nick') or '')[:20]
            row = c.execute('SELECT * FROM market WHERE id=? AND sold=0', (iid,)).fetchone()
            if not row: return {'ok': False, 'error': 'sold_out'}
            if row['seller'] == buyer:
                return {'ok': False, 'error': 'own_item'}
            c.execute('UPDATE market SET sold=1, buyer=? WHERE id=?', (buyer, iid))
            # 판매자 → 골드 우편
            c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) VALUES(?,?,?,?,?,?)',
                      (row['seller'], '거래소', '판매 완료',
                       f"{row['item']} 판매 대금", row['price'], now()))
            # 🛠️ 버그 수정: 구매자에게 아이템 우편 발송 (원래 누락되어 아이템이 증발했음)
            item_json = ''
            try:
                item_json = row['item_json'] or ''
            except Exception:
                item_json = ''
            c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,item_json,ts) '
                      'VALUES(?,?,?,?,0,?,?)',
                      (buyer, '거래소', '구매 상품',
                       f"{row['item']} 구매", item_json, now()))
            return {'ok': True, 'item': dict(row), 'item_json': item_json}

        # ---------- 7. 우편 ----------
        if path == '/mail/list':
            nick = str(P('nick', ''))[:20]
            rows = c.execute('SELECT id,sender,subject,body,gold,item_json FROM mail '
                             'WHERE receiver=? AND taken=0 ORDER BY id DESC LIMIT 30',
                             (nick,)).fetchall()
            return {'ok': True, 'mails': [dict(r) for r in rows]}

        if path == '/mail/take':
            mid = int(body.get('id') or 0)
            nick = (body.get('nick') or '')[:20]
            row = c.execute('SELECT * FROM mail WHERE id=? AND receiver=? AND taken=0',
                            (mid, nick)).fetchone()
            if not row: return {'ok': False, 'error': 'not_found'}
            c.execute('UPDATE mail SET taken=1 WHERE id=?', (mid,))
            return {'ok': True, 'gold': row['gold'], 'item_json': row['item_json'] or '',
                    'subject': row['subject'] or ''}   # 💎 [코인:N] 교환권 마커 인식용

        if path == '/mail/send':
            c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) VALUES(?,?,?,?,?,?)',
                      ((body.get('to') or '')[:20], (body.get('nick') or '')[:20],
                       (body.get('subject') or '쪽지')[:40], (body.get('body') or '')[:200],
                       int(body.get('gold') or 0), now()))
            return {'ok': True}


        # ---------- 8. 경매 ----------
        if path == '/auction/create':
            seller = str(P('nick', ''))[:20]
            item = str(P('item', ''))[:40]
            if not seller or not item:
                return {'ok': False, 'error': 'bad_request'}
            start_price = max(1, int(P('start_price', 1) or 1))
            buyout = max(0, int(P('buyout', 0) or 0))
            if buyout and buyout < start_price:
                return {'ok': False, 'error': 'buyout_too_low'}
            is_private = 1 if P('is_private', 0) in (1, '1', True, 'true') else 0
            code = str(P('code', ''))[:20]
            if is_private and not code:
                return {'ok': False, 'error': 'code_required'}
            minutes = max(1, min(1440, int(P('minutes', 30) or 30)))
            c.execute(
                'INSERT INTO auction(seller,item,grade,item_json,start_price,buyout,'
                'cur_bid,cur_bidder,is_private,code,end_ts,ts) '
                'VALUES(?,?,?,?,?,?,0,"",?,?,?,?)',
                (seller, item, str(P('grade', ''))[:20], str(P('item_json', ''))[:2000],
                 start_price, buyout, is_private, code, now() + minutes * 60, now()))
            return {'ok': True, 'id': c.execute('SELECT last_insert_rowid() i').fetchone()['i']}

        if path == '/auction/list':
            settle_auctions(c)
            code = str(P('code', ''))[:20]
            if code:
                # 비공개 방: 코드가 일치하는 경매만
                rows = c.execute(
                    'SELECT id,seller,item,grade,start_price,buyout,cur_bid,cur_bidder,'
                    'is_private,end_ts FROM auction '
                    'WHERE closed=0 AND is_private=1 AND code=? ORDER BY end_ts ASC LIMIT 50',
                    (code,)).fetchall()
                return {'ok': True, 'items': [dict(r) for r in rows], 'private': True}
            rows = c.execute(
                'SELECT id,seller,item,grade,start_price,buyout,cur_bid,cur_bidder,'
                'is_private,end_ts FROM auction '
                'WHERE closed=0 AND is_private=0 ORDER BY end_ts ASC LIMIT 50').fetchall()
            return {'ok': True, 'items': [dict(r) for r in rows], 'private': False,
                    'now': now()}

        if path == '/auction/bid':
            settle_auctions(c)
            aid = int(P('id', 0) or 0)
            nick = str(P('nick', ''))[:20]
            amount = int(P('amount', 0) or 0)
            row = c.execute('SELECT * FROM auction WHERE id=? AND closed=0', (aid,)).fetchone()
            if not row:
                return {'ok': False, 'error': 'not_found'}
            if row['end_ts'] <= now():
                return {'ok': False, 'error': 'ended'}
            if row['seller'] == nick:
                return {'ok': False, 'error': 'own_auction'}
            if row['is_private'] and str(P('code', '')) != row['code']:
                return {'ok': False, 'error': 'bad_code'}
            floor = max(row['cur_bid'], row['start_price'] - 1)
            if amount <= floor:
                return {'ok': False, 'error': 'low_bid', 'min': floor + 1}
            # 이전 입찰자에게 환불 우편
            if row['cur_bidder'] and row['cur_bid'] > 0:
                c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) '
                          'VALUES(?,?,?,?,?,?)',
                          (row['cur_bidder'], '경매장', '입찰 환불',
                           row['item'] + ' 경매에서 상위 입찰이 발생했습니다.',
                           row['cur_bid'], now()))
            c.execute('UPDATE auction SET cur_bid=?, cur_bidder=? WHERE id=?',
                      (amount, nick, aid))
            c.execute('INSERT INTO auction_bids(auc_id,nick,amount,ts) VALUES(?,?,?,?)',
                      (aid, nick, amount, now()))
            # 즉시구매가 도달 시 즉시 낙찰
            if row['buyout'] and amount >= row['buyout']:
                close_auction(c, aid)
                return {'ok': True, 'won': True, 'amount': amount}
            return {'ok': True, 'won': False, 'amount': amount}

        if path == '/auction/buyout':
            settle_auctions(c)
            aid = int(P('id', 0) or 0)
            nick = str(P('nick', ''))[:20]
            row = c.execute('SELECT * FROM auction WHERE id=? AND closed=0', (aid,)).fetchone()
            if not row:
                return {'ok': False, 'error': 'not_found'}
            if not row['buyout']:
                return {'ok': False, 'error': 'no_buyout'}
            if row['seller'] == nick:
                return {'ok': False, 'error': 'own_auction'}
            if row['is_private'] and str(P('code', '')) != row['code']:
                return {'ok': False, 'error': 'bad_code'}
            if row['cur_bidder'] and row['cur_bid'] > 0:
                c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) '
                          'VALUES(?,?,?,?,?,?)',
                          (row['cur_bidder'], '경매장', '입찰 환불',
                           row['item'] + ' 즉시구매로 종료되었습니다.', row['cur_bid'], now()))
            c.execute('UPDATE auction SET cur_bid=?, cur_bidder=? WHERE id=?',
                      (row['buyout'], nick, aid))
            close_auction(c, aid)
            return {'ok': True, 'amount': row['buyout']}

        if path == '/auction/mine':
            settle_auctions(c)
            nick = str(P('nick', ''))[:20]
            rows = c.execute(
                'SELECT id,item,grade,start_price,buyout,cur_bid,cur_bidder,is_private,'
                'code,end_ts,closed FROM auction WHERE seller=? ORDER BY id DESC LIMIT 30',
                (nick,)).fetchall()
            return {'ok': True, 'items': [dict(r) for r in rows], 'now': now()}

        if path == '/auction/cancel':
            aid = int(P('id', 0) or 0)
            nick = str(P('nick', ''))[:20]
            row = c.execute('SELECT * FROM auction WHERE id=? AND closed=0', (aid,)).fetchone()
            if not row:
                return {'ok': False, 'error': 'not_found'}
            if row['seller'] != nick:
                return {'ok': False, 'error': 'not_owner'}
            if row['cur_bidder']:
                return {'ok': False, 'error': 'has_bid'}   # 입찰이 있으면 취소 불가
            c.execute('UPDATE auction SET closed=1 WHERE id=?', (aid,))
            c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,item_json,ts) '
                      'VALUES(?,?,?,?,0,?,?)',
                      (nick, '경매장', '경매 취소', row['item'] + ' 반환',
                       row['item_json'] or '', now()))
            return {'ok': True}

        if path == '/nick/claim':
            nick = str(P('nick', ''))[:20].strip()
            token = str(P('token', ''))[:64].strip()
            if not nick or not token:
                return {'ok': False, 'error': 'bad_request'}
            # 👑 등급 1~6(개발자 미만 스태프)은 이름 변경 불가:
            #    이 토큰이 소유한 닉 중 스태프 등급이 있으면 다른 닉으로 변경 거부
            #    단, [개발자]가 발행한 강제 개명(rename) 명령이 있으면 허용 + 등급 자동 이전
            owned = c.execute('SELECT nick FROM nick_owner WHERE token=?', (token,)).fetchall()
            for o in owned:
                r0 = rank_of(c, o['nick'])
                if 1 <= r0 <= 6 and o['nick'] != nick:
                    authorized = c.execute(
                        "SELECT 1 FROM admin_cmd WHERE target=? AND field='rename' AND value=?",
                        (o['nick'], nick)).fetchone()
                    if authorized:
                        # 강제 개명 승인됨 → 등급을 새 이름으로 이전하고 이전 소유권 정리
                        c.execute('DELETE FROM admin_ranks WHERE nick=?', (nick,))
                        c.execute('UPDATE admin_ranks SET nick=? WHERE nick=?', (nick, o['nick']))
                        c.execute('DELETE FROM nick_owner WHERE nick=?', (o['nick'],))
                        continue
                    return {'ok': False, 'error': 'rank_locked',
                            'msg': '스태프 등급(%s)은 이름을 변경할 수 없습니다.' % RANK_NAMES[r0]}
            row = c.execute('SELECT token FROM nick_owner WHERE nick=?', (nick,)).fetchone()
            if row and row['token'] != token:
                return {'ok': False, 'error': 'taken'}
            c.execute('INSERT INTO nick_owner(nick,token,ts) VALUES(?,?,?) '
                      'ON CONFLICT(nick) DO UPDATE SET ts=excluded.ts',
                      (nick, token, now()))
            return {'ok': True}

        # ---------- 🚫 오토클리커 감지 / 정지 ----------
        if path == '/click/report':
            # 클라이언트가 최근 클릭 시각(ms) 목록을 주기적으로 보고 → 서버가 판정
            nick = str(P('nick', ''))[:20].strip()
            if not nick:
                return {'ok': True, 'banned': False}
            # 이미 정지 중이면 그대로 알림
            b = get_ban(c, nick)
            if b:
                return {'ok': True, 'banned': True, 'until': b['until'],
                        'seconds': b['until'] - now(), 'interval_ms': b['interval_ms'],
                        'reason': b['reason']}
            ts = body.get('clicks') or body.get('t') or []
            if not isinstance(ts, list):
                ts = []
            # 최대 40개까지만 처리(스팸 방지)
            ts = [int(x) for x in ts[:40] if isinstance(x, (int, float))]
            result = record_clicks_and_check(c, nick, ts)
            if result:
                # 정지 확정
                hrs = result['seconds'] / 3600.0
                return {'ok': True, 'banned': True, 'until': result['until'],
                        'seconds': result['seconds'], 'interval_ms': result['interval_ms'],
                        'reason': 'autoclicker', 'hours': round(hrs, 4)}
            return {'ok': True, 'banned': False}

        if path == '/ban/check':
            nick = str(P('nick', ''))[:20].strip()
            b = get_ban(c, nick)
            if b:
                return {'ok': True, 'banned': True, 'until': b['until'],
                        'seconds': b['until'] - now(), 'interval_ms': b['interval_ms'],
                        'reason': b['reason']}
            return {'ok': True, 'banned': False}

        # ---------- 🎯 관리자 원격 명령 ----------
        if path == '/admin/cmd':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            admin_nick = str(P('admin', ''))[:20].strip()
            my_rank = rank_of(c, admin_nick)
            target = str(P('target', ''))[:20].strip()
            field = str(P('field', ''))[:20].strip()
            value = str(P('value', ''))[:40]
            if not target or field not in ('gold', 'stat', 'stage', 'level', 'job',
                                            'grant_product', 'revoke_product', 'rename'):
                return {'ok': False, 'error': 'bad_request'}
            need = cmd_min_rank(field)
            if my_rank < need:
                return {'ok': False, 'error': 'rank_denied',
                        'need': RANK_NAMES.get(need, str(need)), 'my_rank': RANK_NAMES.get(my_rank)}
            c.execute('INSERT INTO admin_cmd(target,field,value,admin,ts) VALUES(?,?,?,?,?)',
                      (target, field, value, admin_nick, now()))
            return {'ok': True}

        if path == '/admin/pending':
            # 대상 유저가 자기 앞으로 온 미적용 명령을 가져가고 즉시 applied 처리
            nick = str(P('nick', ''))[:20].strip()
            if not nick:
                return {'ok': True, 'commands': []}
            rows = c.execute('SELECT id,field,value FROM admin_cmd WHERE target=? AND applied=0 '
                             'ORDER BY id ASC LIMIT 50', (nick,)).fetchall()
            cmds = [dict(r) for r in rows]
            if rows:
                ids = ','.join(str(r['id']) for r in rows)
                c.execute('UPDATE admin_cmd SET applied=1 WHERE id IN (%s)' % ids)
            return {'ok': True, 'commands': cmds}

        # ---------- 💳 구매 요청 (계좌이체 문의) ----------
        # 유저가 "구매하기" → 계좌이체 후 문의 전송
        if path == '/purchase/request':
            nick = str(P('nick', ''))[:20].strip()
            product = str(P('product', ''))[:40].strip()
            pname = str(P('product_name', ''))[:60]
            depositor = str(P('depositor', ''))[:30]
            if not nick or not product:
                return {'ok': False, 'error': 'bad_request'}
            # 같은 유저의 같은 상품 중복 대기 방지
            dup = c.execute("SELECT id FROM purchase_req WHERE nick=? AND product=? AND status='pending'",
                            (nick, product)).fetchone()
            if dup:
                return {'ok': True, 'dup': True}
            c.execute('INSERT INTO purchase_req(nick,product,product_name,depositor,ts) '
                      'VALUES(?,?,?,?,?)', (nick, product, pname, depositor, now()))
            # 📬 관리자(개발자) 우편함으로 승인 요청 알림 발송
            body = f"[구매 승인 요청]\n유저: {nick}\n상품: {pname or product}"
            if depositor:
                body += f"\n입금자명: {depositor}"
            body += "\n\n개발자 콘솔 > 구매 요청 관리에서 승인해주세요."
            c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) '
                      'VALUES(?,?,?,?,?,?)',
                      (ADMIN_NICK, '💳 구매알림', f'구매 문의: {nick}', body, 0, now()))
            return {'ok': True}

        # 관리자: 대기 중인 구매 요청 목록
        if path == '/purchase/list':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            if rank_of(c, str(P('admin',''))[:20].strip()) < 2:
                return {'ok': False, 'error': 'rank_denied', 'need': RANK_NAMES[2]}
            rows = c.execute("SELECT id,nick,product,product_name,depositor,ts FROM purchase_req "
                             "WHERE status='pending' ORDER BY ts ASC LIMIT 100").fetchall()
            return {'ok': True, 'requests': [dict(r) for r in rows]}

        # 관리자: 구매 요청 승인 → 해당 유저에게 상품 지급 명령 발행
        if path == '/purchase/approve':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            if rank_of(c, str(P('admin',''))[:20].strip()) < 2:
                return {'ok': False, 'error': 'rank_denied', 'need': RANK_NAMES[2]}
            rid = int(P('id', 0) or 0)
            row = c.execute("SELECT nick,product FROM purchase_req WHERE id=? AND status='pending'",
                            (rid,)).fetchone()
            if not row:
                return {'ok': False, 'error': 'not_found'}
            # 상품 지급 명령을 admin_cmd 에 넣어 유저 접속 시 자동 적용
            c.execute('INSERT INTO admin_cmd(target,field,value,admin,ts) VALUES(?,?,?,?,?)',
                      (row['nick'], 'grant_product', row['product'], 'purchase', now()))
            c.execute("UPDATE purchase_req SET status='approved' WHERE id=?", (rid,))
            return {'ok': True, 'nick': row['nick'], 'product': row['product']}

        # 관리자: 구매 요청 거절
        if path == '/purchase/reject':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            if rank_of(c, str(P('admin',''))[:20].strip()) < 2:
                return {'ok': False, 'error': 'rank_denied', 'need': RANK_NAMES[2]}
            rid = int(P('id', 0) or 0)
            c.execute("UPDATE purchase_req SET status='rejected' WHERE id=?", (rid,))
            return {'ok': True}

        # 유저: 내 구매 요청 상태 확인 (승인되면 알림)
        if path == '/purchase/mine':
            nick = str(P('nick', ''))[:20].strip()
            if not nick:
                return {'ok': True, 'requests': []}
            rows = c.execute("SELECT id,product,product_name,status FROM purchase_req "
                             "WHERE nick=? ORDER BY ts DESC LIMIT 20", (nick,)).fetchall()
            return {'ok': True, 'requests': [dict(r) for r in rows]}

        if path == '/admin/ban':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            if rank_of(c, str(P('admin',''))[:20].strip()) < 1:
                return {'ok': False, 'error': 'rank_denied', 'need': RANK_NAMES[1]}
            target = str(P('target', ''))[:20].strip()
            minutes = max(1, int(P('minutes', 60) or 60))
            if not target:
                return {'ok': False, 'error': 'bad_request'}
            until = now() + minutes * 60
            c.execute('INSERT INTO bans(nick,until_ts,reason,interval_ms,ts) VALUES(?,?,?,0,?) '
                      'ON CONFLICT(nick) DO UPDATE SET until_ts=?, reason=?, ts=?',
                      (target, until, 'admin', now(), until, 'admin', now()))
            return {'ok': True, 'until': until}

        # ---------- 👑 개발자 등급 관리 ----------
        if path == '/admin/rank/get':
            # 공개: 닉네임의 등급 조회 (콘솔 접근 판단용 · 등급 숫자만 노출)
            nick = str(P('nick', ''))[:20].strip()
            r0 = rank_of(c, nick)
            return {'ok': True, 'nick': nick, 'rank': r0,
                    'rank_name': RANK_NAMES.get(r0, '일반')}

        if path == '/admin/rank/set':
            # 오직 등급 7(개발자)만 승급/강등 가능
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            admin_nick = str(P('admin', ''))[:20].strip()
            if rank_of(c, admin_nick) < 7:
                return {'ok': False, 'error': 'rank_denied',
                        'msg': '승급/강등은 [개발자] 등급만 가능합니다.'}
            target = str(P('target', ''))[:20].strip()
            new_rank = max(0, min(6, int(P('rank', 0) or 0)))   # 7(개발자)은 부여 불가
            if not target:
                return {'ok': False, 'error': 'bad_request'}
            if target == ADMIN_NICK:
                return {'ok': False, 'error': 'cannot_change_owner'}
            if new_rank == 0:
                c.execute('DELETE FROM admin_ranks WHERE nick=?', (target,))
            else:
                c.execute('INSERT INTO admin_ranks(nick,rank,granted_by,ts) VALUES(?,?,?,?) '
                          'ON CONFLICT(nick) DO UPDATE SET rank=?, granted_by=?, ts=?',
                          (target, new_rank, admin_nick, now(), new_rank, admin_nick, now()))
            # 대상에게 알림 우편
            c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,ts) VALUES(?,?,?,?,0,?)',
                      (target, '운영팀', '등급 변경',
                       '당신의 개발자 등급이 [%s](으)로 변경되었습니다.' % RANK_NAMES.get(new_rank, '일반'),
                       now()))
            return {'ok': True, 'target': target, 'rank': new_rank,
                    'rank_name': RANK_NAMES.get(new_rank)}

        if path == '/admin/rank/list':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            if rank_of(c, str(P('admin',''))[:20].strip()) < 6:
                return {'ok': False, 'error': 'rank_denied'}
            rows = c.execute('SELECT nick,rank,granted_by,ts FROM admin_ranks ORDER BY rank DESC').fetchall()
            out = [dict(r) for r in rows]
            for d in out:
                d['rank_name'] = RANK_NAMES.get(d['rank'], '?')
            return {'ok': True, 'staff': out}

        if path == '/admin/unban':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            if rank_of(c, str(P('admin',''))[:20].strip()) < 1:
                return {'ok': False, 'error': 'rank_denied', 'need': RANK_NAMES[1]}
            target = str(P('target', ''))[:20].strip()
            c.execute('DELETE FROM bans WHERE nick=?', (target,))
            return {'ok': True}

        # ---------- 📢 공지사항 ----------
        if path == '/notice/get':
            row = c.execute("SELECT v FROM meta WHERE k='notice'").fetchone()
            tsr = c.execute("SELECT v FROM meta WHERE k='notice_ts'").fetchone()
            return {'ok': True, 'notice': (row['v'] if row else ''),
                    'ts': int(tsr['v']) if tsr else 0}

        if path == '/notice/set':
            # 등급 5(균열 관리자) 이상 또는 [개발자]만 작성 가능
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth_failed'}
            if rank_of(c, str(P('admin', ''))[:20].strip()) < 5:
                return {'ok': False, 'error': 'rank_denied', 'need': RANK_NAMES[5]}
            text = str(P('text', ''))[:1000]
            c.execute("INSERT INTO meta(k,v) VALUES('notice',?) "
                      "ON CONFLICT(k) DO UPDATE SET v=?", (text, text))
            c.execute("INSERT INTO meta(k,v) VALUES('notice_ts',?) "
                      "ON CONFLICT(k) DO UPDATE SET v=?", (str(now()), str(now())))
            return {'ok': True}

        if path == '/ping':
            # 클라이언트가 접속에 쓸 주소를 서버가 직접 알려준다
            # (EXE/브라우저 어느 쪽에서 접속해도 올바른 주소를 잡도록)
            lan = ''
            try:
                import socket as _sk
                s = _sk.socket(_sk.AF_INET, _sk.SOCK_DGRAM)
                s.connect(('8.8.8.8', 80))
                lan = 'http://%s:%d' % (s.getsockname()[0], PORT)
                s.close()
            except Exception:
                pass
            return {'ok': True, 'server': 'rift', 'time': now(),
                    'port': PORT, 'lan_url': lan,
                    'local_url': 'http://127.0.0.1:%d' % PORT}

    return {'ok': False, 'error': 'unknown_endpoint'}