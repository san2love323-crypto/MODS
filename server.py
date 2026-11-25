 # -*- coding: utf-8 -*-
"""
🌐 차원 균열의 만물상 — 온라인 서버
표준 라이브러리만 사용 (설치 불필요)

실행: python server.py
기본 포트: 8777

기능: 채팅 / 랭킹 / 길드 / 접속자 / 월드보스 / 거래소 / 우편
"""
import json, os, sys, time, threading, sqlite3, hashlib
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
from socketserver import ThreadingMixIn
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
    '/friend/list', '/friend/remove', '/invite/send', '/invite/poll', '/invite/respond',
    '/party/state', '/party/create', '/party/leave',
    '/guild/relation/set', '/guild/relation/list', '/clanwar/list',
    '/pvp/rank', '/pvp/mymatch', '/pvp/join', '/pvp/leave', '/pvp/action', '/pvp/state',
    '/rtc/send', '/rtc/poll',
    '/save/upload', '/save/download',
    '/admin/backup/list', '/admin/backup/now', '/admin/backup/restore',
}
SERVER_VER = 3   # 소셜(친구/파티/음성/클랜전/PvP/백업) 지원 버전

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

        -- 👥 친구
        CREATE TABLE IF NOT EXISTS friends(
            a TEXT NOT NULL, b TEXT NOT NULL, ts INTEGER NOT NULL,
            PRIMARY KEY(a, b));

        -- 📨 초대/신청 (friend·party·duel·guild·joinreq·clanwar)
        CREATE TABLE IF NOT EXISTS invites(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type TEXT NOT NULL,
            from_nick TEXT NOT NULL, to_nick TEXT NOT NULL,
            data TEXT DEFAULT '{}', status TEXT DEFAULT 'pending',
            resp_ts INTEGER DEFAULT 0, ts INTEGER NOT NULL);
        CREATE INDEX IF NOT EXISTS idx_inv_to ON invites(to_nick, status);

        -- 🏰 클랜 관계 (동맹/적대)  g1<g2 정렬 저장
        CREATE TABLE IF NOT EXISTS guild_relations(
            g1 TEXT NOT NULL, g2 TEXT NOT NULL,
            type TEXT NOT NULL,            -- ally_pending / ally / enemy
            req TEXT DEFAULT '',           -- 동맹을 신청한 길드
            ts INTEGER NOT NULL,
            PRIMARY KEY(g1, g2));

        -- 📅 클랜전 (예약제)
        CREATE TABLE IF NOT EXISTS clan_wars(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            g1 TEXT NOT NULL, g2 TEXT NOT NULL,
            date INTEGER NOT NULL, status TEXT DEFAULT 'scheduled',
            score1 REAL DEFAULT 0, score2 REAL DEFAULT 0,
            winner TEXT DEFAULT '', ts INTEGER NOT NULL);

        -- 🏆 PvP 전적
        CREATE TABLE IF NOT EXISTS pvp_stats(
            nick TEXT PRIMARY KEY, wins INTEGER DEFAULT 0,
            losses INTEGER DEFAULT 0, ts INTEGER NOT NULL);

        -- 💾 유저 세이브 서버 보관 (서버 초기화 복구용)
        CREATE TABLE IF NOT EXISTS saves(
            nick TEXT PRIMARY KEY,
            token TEXT DEFAULT '',
            data TEXT NOT NULL,
            power REAL DEFAULT 0,
            stage INTEGER DEFAULT 1,
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


# ============================== 💾 자동 백업 & 복구 ==============================
import random as _rd

BACKUP_DIR = os.path.join(os.path.dirname(os.path.abspath(DB_PATH)) or '.', 'backups')
BACKUP_EVERY = 30 * 60      # 30분마다
BACKUP_KEEP = 60            # 최근 60개 보관 (30시간 분량)

def do_backup():
    try:
        os.makedirs(BACKUP_DIR, exist_ok=True)
        fname = time.strftime('rift_%Y%m%d_%H%M%S.db')
        dst = os.path.join(BACKUP_DIR, fname)
        src = sqlite3.connect(DB_PATH)
        out = sqlite3.connect(dst)
        with out:
            src.backup(out)
        out.close(); src.close()
        files = sorted(f for f in os.listdir(BACKUP_DIR)
                       if f.startswith('rift_') and f.endswith('.db'))
        for f in files[:-BACKUP_KEEP]:
            try: os.remove(os.path.join(BACKUP_DIR, f))
            except Exception: pass
        return fname
    except Exception as e:
        print('[백업 실패]', e)
        return None

def backup_loop():
    while True:
        time.sleep(BACKUP_EVERY)
        do_backup()

def list_backups():
    try:
        os.makedirs(BACKUP_DIR, exist_ok=True)
        out = []
        for f in sorted(os.listdir(BACKUP_DIR), reverse=True):
            if not (f.startswith('rift_') and f.endswith('.db')):
                continue
            p = os.path.join(BACKUP_DIR, f)
            st = os.stat(p)
            out.append({'file': f, 'size': st.st_size, 'ts': int(st.st_mtime)})
        return out[:BACKUP_KEEP]
    except Exception:
        return []

def restore_from_backup(c, fname):
    """백업 DB의 유저 데이터를 현재 DB에 '병합' 복구.
    - saves: 없는 유저는 추가, 백업이 더 최신이면 교체
    - ranking/players/guilds/guild_members/nick_owner/admin_ranks: 없는 행만 추가
    - mail: 안 받은(taken=0) 우편 중 없는 것 추가
    현재 데이터는 절대 삭제하지 않는다."""
    if '/' in fname or '\\' in fname or '..' in fname:
        return {'ok': False, 'error': 'bad_name'}
    path = os.path.join(BACKUP_DIR, fname)
    if not os.path.isfile(path):
        return {'ok': False, 'error': 'not_found'}
    b = sqlite3.connect('file:%s?mode=ro' % path, uri=True)
    b.row_factory = sqlite3.Row
    stat = {}
    def has_table(name):
        return b.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
                         (name,)).fetchone() is not None
    try:
        if has_table('saves'):
            n = 0
            for r in b.execute('SELECT nick,token,data,power,stage,ts FROM saves'):
                cur = c.execute('SELECT ts FROM saves WHERE nick=?', (r['nick'],)).fetchone()
                if cur is None or (cur['ts'] or 0) < (r['ts'] or 0):
                    c.execute('INSERT INTO saves(nick,token,data,power,stage,ts) VALUES(?,?,?,?,?,?) '
                              'ON CONFLICT(nick) DO UPDATE SET token=excluded.token, data=excluded.data,'
                              'power=excluded.power, stage=excluded.stage, ts=excluded.ts',
                              (r['nick'], r['token'], r['data'], r['power'], r['stage'], r['ts']))
                    n += 1
            stat['세이브'] = n
        merge = {
            'ranking': ('랭킹', 'nick,power,stage,level,tier,ts'),
            'players': ('플레이어', 'nick,last_seen,stage,level,guild'),
            'guilds': ('길드', 'name,owner,notice,score,raid_score,ts'),
            'guild_members': ('길드원', 'guild,nick,ts'),
            'nick_owner': ('닉네임소유권', 'nick,token,ts'),
            'admin_ranks': ('관리자등급', 'nick,rank,granted_by,ts'),
        }
        for tbl, (ko, cols) in merge.items():
            if not has_table(tbl):
                continue
            n = 0
            ph = ','.join('?' * len(cols.split(',')))
            for r in b.execute('SELECT %s FROM %s' % (cols, tbl)):
                cu = c.execute('INSERT OR IGNORE INTO %s(%s) VALUES(%s)' % (tbl, cols, ph), tuple(r))
                n += cu.rowcount
            stat[ko] = n
        if has_table('mail'):
            n = 0
            for r in b.execute('SELECT receiver,sender,subject,body,gold,item_json,ts FROM mail WHERE taken=0'):
                dup = c.execute('SELECT 1 FROM mail WHERE receiver=? AND sender=? AND subject=? AND ts=?',
                                (r['receiver'], r['sender'], r['subject'], r['ts'])).fetchone()
                if not dup:
                    c.execute('INSERT INTO mail(receiver,sender,subject,body,gold,item_json,taken,ts) '
                              'VALUES(?,?,?,?,?,?,0,?)',
                              (r['receiver'], r['sender'], r['subject'], r['body'],
                               r['gold'], r['item_json'], r['ts']))
                    n += 1
            stat['우편'] = n
    finally:
        b.close()
    return {'ok': True, 'restored': stat, 'file': fname}


# ============================== 🎉 파티 & 🎤 음성 시그널링 ==============================
PARTIES = {}      # id -> {id, owner, members:[nick], ts}
PARTY_SEQ = [0]
RTC = {}          # room -> [{id, from, to, payload, ts}]
RTC_SEQ = [0]

def _party_of(nick):
    for pid, p in PARTIES.items():
        if nick in p['members']:
            return p
    return None

def _party_clean():
    t = now()
    dead = [pid for pid, p in PARTIES.items() if not p['members'] or t - p['ts'] > 21600]
    for pid in dead:
        PARTIES.pop(pid, None)

def _rtc_clean(room):
    t = now()
    if room in RTC:
        RTC[room] = [m for m in RTC[room] if t - m['ts'] < 120][-200:]
    dead = [r for r, ms in RTC.items() if not ms or all(t - m['ts'] > 300 for m in ms)]
    for r in dead:
        if r != room:
            RTC.pop(r, None)

def _clanwar_tick(c):
    t = now()
    c.execute("UPDATE clan_wars SET status='active' WHERE status='scheduled' AND date<=?", (t,))
    for r in c.execute("SELECT * FROM clan_wars WHERE status='active' AND date+172800<=?", (t,)).fetchall():
        w = r['g1'] if r['score1'] > r['score2'] else (r['g2'] if r['score2'] > r['score1'] else '무승부')
        c.execute("UPDATE clan_wars SET status='done', winner=? WHERE id=?", (w, r['id']))

def _pvp_finish(c, m, winner):
    """PvP 종료 시 1회: 전적 기록 + 클랜전 점수"""
    if m.get('recorded'):
        return
    m['recorded'] = True
    losers = [n for n in m['p'] if n != winner]
    if not losers:
        return
    loser = losers[0]
    t = now()
    c.execute('INSERT INTO pvp_stats(nick,wins,losses,ts) VALUES(?,1,0,?) '
              'ON CONFLICT(nick) DO UPDATE SET wins=wins+1, ts=?', (winner, t, t))
    c.execute('INSERT INTO pvp_stats(nick,wins,losses,ts) VALUES(?,0,1,?) '
              'ON CONFLICT(nick) DO UPDATE SET losses=losses+1, ts=?', (loser, t, t))
    _clanwar_tick(c)
    gw = c.execute('SELECT guild FROM players WHERE nick=?', (winner,)).fetchone()
    gl = c.execute('SELECT guild FROM players WHERE nick=?', (loser,)).fetchone()
    gw = gw['guild'] if gw else ''
    gl = gl['guild'] if gl else ''
    if gw and gl and gw != gl:
        r = c.execute("SELECT * FROM clan_wars WHERE status='active' AND "
                      "((g1=? AND g2=?) OR (g1=? AND g2=?))", (gw, gl, gl, gw)).fetchone()
        if r:
            col = 'score1' if r['g1'] == gw else 'score2'
            c.execute('UPDATE clan_wars SET %s=%s+1 WHERE id=?' % (col, col), (r['id'],))

def _guild_owner(c, name):
    r = c.execute('SELECT owner FROM guilds WHERE name=?', (name,)).fetchone()
    return r['owner'] if r else None

def _rel_key(a, b):
    return (a, b) if a < b else (b, a)


# ============================== ⚔️ PvP 실시간 대전 ==============================
PVP_QUEUE = []       # [{nick, power, loadout, ts, enq}]
PVP_MATCHES = {}     # id -> match
PVP_SEQ = [0]

def _pvp_clean():
    t = now()
    PVP_QUEUE[:] = [q for q in PVP_QUEUE if t - q['ts'] < 90]
    dead = [mid for mid, m in PVP_MATCHES.items()
            if (m['winner'] and t - m['won_ts'] > 60) or t - m['ts'] > 900]
    for mid in dead:
        PVP_MATCHES.pop(mid, None)

def _pvp_match_of(nick):
    for mid, m in PVP_MATCHES.items():
        if not m['winner'] and nick in m['p']:
            return mid, m
    return None, None


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
            # 🏰 클랜은 초대제 — 직접 가입 불가, 가입 신청(joinreq) 후 클랜장이 수락해야 함
            return {'ok': False, 'error': 'invite_only'}

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

        # ---------- 👥 친구 ----------
        if path == '/friend/list':
            nick = str(P('nick', ''))[:20].strip()
            cut = now() - 90
            rows = c.execute('SELECT b, ts FROM friends WHERE a=? ORDER BY ts DESC LIMIT 100', (nick,)).fetchall()
            out = []
            for r in rows:
                on = c.execute('SELECT 1 FROM players WHERE nick=? AND last_seen>?', (r['b'], cut)).fetchone()
                out.append({'nick': r['b'], 'online': bool(on)})
            return {'ok': True, 'friends': out}

        if path == '/friend/remove':
            nick = str(P('nick', ''))[:20].strip()
            tg = str(P('target', ''))[:20].strip()
            c.execute('DELETE FROM friends WHERE (a=? AND b=?) OR (a=? AND b=?)', (nick, tg, tg, nick))
            return {'ok': True}

        # ---------- 📨 초대/신청 ----------
        if path == '/invite/send':
            typ = str(P('type', ''))[:12]
            frm = str(P('from', ''))[:20].strip()
            to = str(P('to', ''))[:20].strip()
            data = P('data', {}) or {}
            if isinstance(data, str):
                try: data = json.loads(data)
                except Exception: data = {}
            if typ not in ('friend', 'party', 'duel', 'guild', 'joinreq', 'clanwar'):
                return {'ok': False, 'error': 'bad_type'}
            if not frm:
                return {'ok': False, 'error': 'no_from'}
            c.execute("DELETE FROM invites WHERE status='pending' AND ts<?", (now() - 86400,))
            if typ == 'guild':
                g = str(data.get('guild', ''))[:30]
                if _guild_owner(c, g) != frm:
                    return {'ok': False, 'error': 'not_owner'}
                if not to:
                    return {'ok': False, 'error': 'no_to'}
            if typ == 'joinreq':
                g = str(data.get('guild', ''))[:30]
                own = _guild_owner(c, g)
                if not own:
                    return {'ok': False, 'error': 'no_guild'}
                to = own
            if typ == 'clanwar':
                g1 = str(data.get('from_guild', ''))[:30]
                g2 = str(data.get('target', ''))[:30]
                date = int(data.get('date', 0) or 0)
                if _guild_owner(c, g1) != frm:
                    return {'ok': False, 'error': 'not_owner'}
                own2 = _guild_owner(c, g2)
                if not own2:
                    return {'ok': False, 'error': 'no_guild'}
                if date <= now():
                    return {'ok': False, 'error': 'bad_date'}
                to = own2
                data = {'from_guild': g1, 'target': g2, 'date': date}
            if typ == 'party':
                p = _party_of(frm)
                if p is None:
                    _party_clean()
                    PARTY_SEQ[0] += 1
                    pid = 'p%d_%d' % (now(), PARTY_SEQ[0])
                    p = {'id': pid, 'owner': frm, 'members': [frm], 'ts': now()}
                    PARTIES[pid] = p
                data = {'party': p['id']}
            if typ == 'friend' and c.execute('SELECT 1 FROM friends WHERE a=? AND b=?', (frm, to)).fetchone():
                return {'ok': False, 'error': 'already_friend'}
            if not to:
                return {'ok': False, 'error': 'no_to'}
            if to == frm:
                return {'ok': False, 'error': 'self'}
            if c.execute("SELECT 1 FROM invites WHERE type=? AND from_nick=? AND to_nick=? AND status='pending'",
                         (typ, frm, to)).fetchone():
                return {'ok': False, 'error': 'dup'}
            c.execute('INSERT INTO invites(type,from_nick,to_nick,data,status,ts) VALUES(?,?,?,?,?,?)',
                      (typ, frm, to, json.dumps(data, ensure_ascii=False), 'pending', now()))
            return {'ok': True, 'to': to}

        if path == '/invite/poll':
            nick = str(P('nick', ''))[:20].strip()
            since_resp = int(P('since_resp', 0) or 0)
            rows = c.execute("SELECT id,type,from_nick,data,ts FROM invites WHERE to_nick=? "
                             "AND status='pending' ORDER BY id DESC LIMIT 20", (nick,)).fetchall()
            resp = c.execute("SELECT id,type,to_nick,status,data,resp_ts FROM invites WHERE from_nick=? "
                             "AND status IN ('accepted','declined') AND resp_ts>? "
                             "ORDER BY resp_ts DESC LIMIT 20", (nick, since_resp)).fetchall()
            return {'ok': True,
                    'invites': [dict(r) for r in rows],
                    'responses': [dict(r) for r in resp]}

        if path == '/invite/respond':
            iid = int(P('id', 0) or 0)
            nick = str(P('nick', ''))[:20].strip()
            accept = bool(P('accept', False))
            iv = c.execute('SELECT * FROM invites WHERE id=?', (iid,)).fetchone()
            if not iv or iv['to_nick'] != nick or iv['status'] != 'pending':
                return {'ok': False, 'error': 'gone'}
            try:
                data = json.loads(iv['data'] or '{}')
            except Exception:
                data = {}
            c.execute('UPDATE invites SET status=?, resp_ts=? WHERE id=?',
                      ('accepted' if accept else 'declined', now(), iid))
            if not accept:
                return {'ok': True, 'declined': True}
            typ = iv['type']
            frm = iv['from_nick']
            if typ == 'friend':
                c.execute('INSERT OR IGNORE INTO friends(a,b,ts) VALUES(?,?,?)', (frm, nick, now()))
                c.execute('INSERT OR IGNORE INTO friends(a,b,ts) VALUES(?,?,?)', (nick, frm, now()))
                return {'ok': True, 'type': typ, 'friend': frm}
            if typ == 'party':
                pid = data.get('party', '')
                p = PARTIES.get(pid)
                if p is None:
                    return {'ok': False, 'error': 'party_gone'}
                if len(p['members']) >= 8:
                    return {'ok': False, 'error': 'party_full'}
                old = _party_of(nick)
                if old and old['id'] != pid:
                    old['members'] = [x for x in old['members'] if x != nick]
                if nick not in p['members']:
                    p['members'].append(nick)
                p['ts'] = now()
                return {'ok': True, 'type': typ, 'party': p}
            if typ in ('guild', 'joinreq'):
                g = str(data.get('guild', ''))[:30]
                member = nick if typ == 'guild' else frm
                if not c.execute('SELECT 1 FROM guilds WHERE name=?', (g,)).fetchone():
                    return {'ok': False, 'error': 'no_guild'}
                c.execute('DELETE FROM guild_members WHERE nick=?', (member,))
                c.execute('INSERT OR REPLACE INTO guild_members(guild,nick,ts) VALUES(?,?,?)',
                          (g, member, now()))
                c.execute('UPDATE players SET guild=? WHERE nick=?', (g, member))
                return {'ok': True, 'type': typ, 'guild': g, 'member': member}
            if typ == 'duel':
                if _pvp_match_of(frm)[1] or _pvp_match_of(nick)[1]:
                    return {'ok': False, 'error': 'busy'}
                ld1 = data.get('loadout', {})
                p1 = max(1.0, float(data.get('power', 1) or 1))
                ld2 = P('loadout', {}) or {}
                if isinstance(ld2, str):
                    try: ld2 = json.loads(ld2)
                    except Exception: ld2 = {}
                p2 = max(1.0, float(P('power', 1) or 1))
                PVP_SEQ[0] += 1
                mid = 'm%d_%d' % (now(), PVP_SEQ[0])
                PVP_MATCHES[mid] = {
                    'p': {frm: {'hp': 100.0, 'power': p1, 'loadout': ld1, 'last_atk': 0, 'seen': now()},
                          nick: {'hp': 100.0, 'power': p2, 'loadout': ld2, 'last_atk': 0, 'seen': now()}},
                    'actions': [], 'seq': 0, 'winner': '', 'won_ts': 0, 'ts': now()}
                return {'ok': True, 'type': typ, 'matched': True, 'match': mid,
                        'opp': ld1, 'opp_power': p1}
            if typ == 'clanwar':
                g1 = data.get('from_guild', '')
                g2 = data.get('target', '')
                date = int(data.get('date', 0) or 0)
                c.execute('INSERT INTO clan_wars(g1,g2,date,status,ts) VALUES(?,?,?,?,?)',
                          (g1, g2, date, 'scheduled', now()))
                return {'ok': True, 'type': typ, 'war': {'g1': g1, 'g2': g2, 'date': date}}
            return {'ok': True, 'type': typ}

        # ---------- 🎉 파티 ----------
        if path == '/party/state':
            _party_clean()
            nick = str(P('nick', ''))[:20].strip()
            p = _party_of(nick)
            if p is None:
                return {'ok': True, 'party': None}
            cut = now() - 90
            mem = []
            for n in p['members']:
                on = c.execute('SELECT 1 FROM players WHERE nick=? AND last_seen>?', (n, cut)).fetchone()
                mem.append({'nick': n, 'online': bool(on), 'owner': n == p['owner']})
            return {'ok': True, 'party': {'id': p['id'], 'owner': p['owner'], 'members': mem}}

        if path == '/party/create':
            nick = str(P('nick', ''))[:20].strip()
            if not nick:
                return {'ok': False, 'error': 'no_nick'}
            p = _party_of(nick)
            if p is None:
                PARTY_SEQ[0] += 1
                pid = 'p%d_%d' % (now(), PARTY_SEQ[0])
                p = {'id': pid, 'owner': nick, 'members': [nick], 'ts': now()}
                PARTIES[pid] = p
            return {'ok': True, 'party': p}

        if path == '/party/leave':
            nick = str(P('nick', ''))[:20].strip()
            p = _party_of(nick)
            if p:
                p['members'] = [x for x in p['members'] if x != nick]
                if p['owner'] == nick and p['members']:
                    p['owner'] = p['members'][0]
                if not p['members']:
                    PARTIES.pop(p['id'], None)
            return {'ok': True}

        # ---------- 🏰 클랜 관계 (동맹/적대) ----------
        if path == '/guild/relation/set':
            nick = str(P('nick', ''))[:20].strip()
            target = str(P('target', ''))[:30].strip()
            typ = str(P('rel', ''))[:12]
            g = c.execute('SELECT guild FROM guild_members WHERE nick=?', (nick,)).fetchone()
            g = g['guild'] if g else ''
            if not g or _guild_owner(c, g) != nick:
                return {'ok': False, 'error': 'not_owner'}
            if not c.execute('SELECT 1 FROM guilds WHERE name=?', (target,)).fetchone():
                return {'ok': False, 'error': 'no_guild'}
            if target == g:
                return {'ok': False, 'error': 'self'}
            k1, k2 = _rel_key(g, target)
            cur = c.execute('SELECT * FROM guild_relations WHERE g1=? AND g2=?', (k1, k2)).fetchone()
            if typ == 'none':
                c.execute('DELETE FROM guild_relations WHERE g1=? AND g2=?', (k1, k2))
                return {'ok': True, 'rel': 'none'}
            if typ == 'enemy':
                c.execute('INSERT OR REPLACE INTO guild_relations(g1,g2,type,req,ts) VALUES(?,?,?,?,?)',
                          (k1, k2, 'enemy', g, now()))
                return {'ok': True, 'rel': 'enemy'}
            if typ == 'ally':
                if cur and cur['type'] == 'ally_pending' and cur['req'] != g:
                    c.execute('UPDATE guild_relations SET type=?, ts=? WHERE g1=? AND g2=?',
                              ('ally', now(), k1, k2))
                    return {'ok': True, 'rel': 'ally'}
                c.execute('INSERT OR REPLACE INTO guild_relations(g1,g2,type,req,ts) VALUES(?,?,?,?,?)',
                          (k1, k2, 'ally_pending', g, now()))
                return {'ok': True, 'rel': 'ally_pending'}
            return {'ok': False, 'error': 'bad_rel'}

        if path == '/guild/relation/list':
            g = str(P('guild', ''))[:30].strip()
            rows = c.execute('SELECT * FROM guild_relations WHERE g1=? OR g2=? ORDER BY ts DESC LIMIT 50',
                             (g, g)).fetchall()
            out = []
            for r in rows:
                other = r['g2'] if r['g1'] == g else r['g1']
                out.append({'guild': other, 'type': r['type'], 'req': r['req']})
            return {'ok': True, 'relations': out}

        # ---------- 📅 클랜전 ----------
        if path == '/clanwar/list':
            _clanwar_tick(c)
            g = str(P('guild', ''))[:30].strip()
            if g:
                rows = c.execute('SELECT * FROM clan_wars WHERE g1=? OR g2=? ORDER BY date DESC LIMIT 30',
                                 (g, g)).fetchall()
            else:
                rows = c.execute('SELECT * FROM clan_wars ORDER BY date DESC LIMIT 30').fetchall()
            return {'ok': True, 'wars': [dict(r) for r in rows], 'now': now()}

        # ---------- 🏆 PvP 랭킹 / 내 매치 ----------
        if path == '/pvp/rank':
            rows = c.execute('SELECT nick,wins,losses FROM pvp_stats '
                             'ORDER BY wins DESC, losses ASC LIMIT 50').fetchall()
            return {'ok': True, 'rank': [dict(r) for r in rows]}

        if path == '/pvp/mymatch':
            nick = str(P('nick', ''))[:20].strip()
            mid, m = _pvp_match_of(nick)
            if not m:
                return {'ok': True, 'matched': False}
            opp = [n for n in m['p'] if n != nick][0]
            return {'ok': True, 'matched': True, 'match': mid,
                    'opp': m['p'][opp]['loadout'], 'opp_power': m['p'][opp]['power']}

        # ---------- 🎤 음성채팅 시그널링 (WebRTC) ----------
        if path == '/rtc/send':
            room = str(P('room', ''))[:60]
            frm = str(P('from', ''))[:20].strip()
            to = str(P('to', '*'))[:20]
            payload = P('payload', {})
            if not room or not frm:
                return {'ok': False, 'error': 'empty'}
            _rtc_clean(room)
            RTC_SEQ[0] += 1
            RTC.setdefault(room, []).append(
                {'id': RTC_SEQ[0], 'from': frm, 'to': to, 'payload': payload, 'ts': now()})
            return {'ok': True, 'id': RTC_SEQ[0]}

        if path == '/rtc/poll':
            room = str(P('room', ''))[:60]
            nick = str(P('nick', ''))[:20].strip()
            since = int(P('since', 0) or 0)
            msgs = [m for m in RTC.get(room, [])
                    if m['id'] > since and m['from'] != nick and m['to'] in ('*', nick)]
            top = max([m['id'] for m in RTC.get(room, [])] + [since])
            return {'ok': True, 'msgs': msgs[-40:], 'seq': top}

        # ---------- 💾 세이브 서버 보관 ----------
        if path == '/save/upload':
            nick = str(P('nick', ''))[:20].strip()
            token = str(P('token', ''))[:64].strip()
            data = P('data', '')
            if not isinstance(data, str):
                data = json.dumps(data, ensure_ascii=False)
            if not nick or not data:
                return {'ok': False, 'error': 'empty'}
            if len(data) > 800000:
                return {'ok': False, 'error': 'too_big'}
            row = c.execute('SELECT token FROM nick_owner WHERE nick=?', (nick,)).fetchone()
            if row and row['token'] and token and row['token'] != token:
                return {'ok': False, 'error': 'not_owner'}
            c.execute('INSERT INTO saves(nick,token,data,power,stage,ts) VALUES(?,?,?,?,?,?) '
                      'ON CONFLICT(nick) DO UPDATE SET data=excluded.data, power=excluded.power,'
                      'stage=excluded.stage, ts=excluded.ts',
                      (nick, token, data, float(P('power', 0) or 0), int(P('stage', 1) or 1), now()))
            return {'ok': True}

        if path == '/save/download':
            nick = str(P('nick', ''))[:20].strip()
            token = str(P('token', ''))[:64].strip()
            r = c.execute('SELECT data,ts,stage,power FROM saves WHERE nick=?', (nick,)).fetchone()
            if not r:
                return {'ok': False, 'error': 'no_save'}
            own = c.execute('SELECT token FROM nick_owner WHERE nick=?', (nick,)).fetchone()
            if own and own['token'] and token and own['token'] != token:
                return {'ok': False, 'error': 'not_owner'}
            return {'ok': True, 'data': r['data'], 'ts': r['ts'],
                    'stage': r['stage'], 'power': r['power']}

        # ---------- 💾 관리자 백업/복구 ----------
        if path == '/admin/backup/list':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth'}
            return {'ok': True, 'backups': list_backups(),
                    'saved_users': c.execute('SELECT COUNT(*) n FROM saves').fetchone()['n']}

        if path == '/admin/backup/now':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth'}
            f = do_backup()
            return {'ok': bool(f), 'file': f}

        if path == '/admin/backup/restore':
            if str(P('pass', '')) != ADMIN_PASSWORD:
                return {'ok': False, 'error': 'auth'}
            return restore_from_backup(c, str(P('file', '')))

        # ---------- ⚔️ PvP ----------
        if path == '/pvp/join':
            _pvp_clean()
            nick = str(P('nick', ''))[:20].strip()
            token = str(P('token', ''))[:64].strip()
            if not nick:
                return {'ok': False, 'error': 'no_nick'}
            own = c.execute('SELECT token FROM nick_owner WHERE nick=?', (nick,)).fetchone()
            if own and own['token'] and token and own['token'] != token:
                return {'ok': False, 'error': 'not_owner'}
            mid, m = _pvp_match_of(nick)
            if m:
                opp = [n for n in m['p'] if n != nick][0]
                return {'ok': True, 'matched': True, 'match': mid,
                        'opp': m['p'][opp]['loadout'], 'opp_power': m['p'][opp]['power']}
            power = max(1.0, float(P('power', 1) or 1))
            loadout = P('loadout', {})
            if isinstance(loadout, str):
                try: loadout = json.loads(loadout)
                except Exception: loadout = {}
            me = None
            for q in PVP_QUEUE:
                if q['nick'] == nick:
                    q['power'] = power; q['loadout'] = loadout; me = q
                    break
            if me is None:
                me = {'nick': nick, 'power': power, 'loadout': loadout,
                      'ts': now(), 'enq': now()}
                PVP_QUEUE.append(me)
            me['ts'] = now()
            # 전투력 제한 없음 — 대기 중인 상대가 있으면 바로 매칭
            best = None
            for q in PVP_QUEUE:
                if q['nick'] == nick:
                    continue
                best = (0, q)
                break
            if best:
                q = best[1]
                PVP_QUEUE[:] = [x for x in PVP_QUEUE if x['nick'] not in (nick, q['nick'])]
                PVP_SEQ[0] += 1
                mid = 'm%d_%d' % (now(), PVP_SEQ[0])
                PVP_MATCHES[mid] = {
                    'p': {nick: {'hp': 100.0, 'power': power, 'loadout': loadout,
                                 'last_atk': 0, 'seen': now()},
                          q['nick']: {'hp': 100.0, 'power': q['power'], 'loadout': q['loadout'],
                                      'last_atk': 0, 'seen': now()}},
                    'actions': [], 'seq': 0, 'winner': '', 'won_ts': 0, 'ts': now()}
                return {'ok': True, 'matched': True, 'match': mid,
                        'opp': q['loadout'], 'opp_power': q['power']}
            return {'ok': True, 'matched': False, 'waiting': len(PVP_QUEUE)}

        if path == '/pvp/leave':
            nick = str(P('nick', ''))[:20].strip()
            PVP_QUEUE[:] = [q for q in PVP_QUEUE if q['nick'] != nick]
            mid, m = _pvp_match_of(nick)
            if m:
                opp = [n for n in m['p'] if n != nick][0]
                m['winner'] = opp; m['won_ts'] = now(); m['forfeit'] = True
                _pvp_finish(c, m, opp)
            return {'ok': True}

        if path == '/pvp/action':
            mid = str(P('match', ''))
            nick = str(P('nick', ''))[:20].strip()
            m = PVP_MATCHES.get(mid)
            if not m or nick not in m['p']:
                return {'ok': False, 'error': 'no_match'}
            if m['winner']:
                return {'ok': True, 'winner': m['winner'],
                        'hp': {n: p['hp'] for n, p in m['p'].items()}}
            me = m['p'][nick]
            opp_nick = [n for n in m['p'] if n != nick][0]
            opp = m['p'][opp_nick]
            t_ms = time.time() * 1000
            if t_ms - me['last_atk'] < 110:      # 서버측 연타(매크로) 제한
                return {'ok': True, 'ignored': True, 'opp_hp': opp['hp'],
                        'winner': m['winner'], 'seq': m['seq']}
            me['last_atk'] = t_ms; me['seen'] = now()
            # 데미지: 전투력 비율 기반 % 데미지 (매칭이 비슷해서 1.6~9% 사이)
            ratio = (me['power'] / max(1.0, opp['power'])) ** 0.5
            dmg = max(1.6, min(9.0, 4.0 * ratio))
            crit = _rd.random() < 0.15
            if crit:
                dmg *= 1.8
            dmg = round(dmg, 2)
            opp['hp'] = max(0.0, opp['hp'] - dmg)
            m['seq'] += 1
            m['actions'].append({'seq': m['seq'], 'by': nick, 'dmg': dmg,
                                 'crit': crit, 'hp': opp['hp']})
            if len(m['actions']) > 120:
                m['actions'] = m['actions'][-120:]
            if opp['hp'] <= 0:
                m['winner'] = nick; m['won_ts'] = now()
                _pvp_finish(c, m, nick)
            return {'ok': True, 'dmg': dmg, 'crit': crit, 'opp_hp': opp['hp'],
                    'winner': m['winner'], 'seq': m['seq']}

        if path == '/pvp/state':
            mid = str(P('match', ''))
            nick = str(P('nick', ''))[:20].strip()
            since = int(P('since', 0) or 0)
            m = PVP_MATCHES.get(mid)
            if not m or nick not in m['p']:
                return {'ok': False, 'error': 'no_match'}
            m['p'][nick]['seen'] = now()
            opp_nick = [n for n in m['p'] if n != nick][0]
            if not m['winner'] and now() - m['p'][opp_nick]['seen'] > 12:
                m['winner'] = nick; m['won_ts'] = now(); m['forfeit'] = True   # 상대 이탈 = 몰수승
                _pvp_finish(c, m, nick)
            acts = [a for a in m['actions'] if a['seq'] > since]
            return {'ok': True, 'hp': {n: p['hp'] for n, p in m['p'].items()},
                    'actions': acts, 'seq': m['seq'], 'winner': m['winner'],
                    'forfeit': bool(m.get('forfeit'))}

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
            return {'ver': SERVER_VER, 'ok': True, 'server': 'rift', 'time': now(),
                    'port': PORT, 'lan_url': lan,
                    'local_url': 'http://127.0.0.1:%d' % PORT}

    return {'ok': False, 'error': 'unknown_endpoint'}


class Handler(BaseHTTPRequestHandler):
    def _send(self, obj, code=200):
        try:
            data = json.dumps(obj, ensure_ascii=False).encode('utf-8')
            self.send_response(code)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Access-Control-Allow-Headers', 'Content-Type')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass   # 클라이언트가 먼저 연결을 끊음 (탭 전환 등) — 무시

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.send_header('Access-Control-Allow-Methods', 'GET,POST,OPTIONS')
        self.end_headers()

    def _serve_static(self, url_path):
        rel = url_path.split('?')[0]
        if rel in ('', '/'):
            rel = '/index.html'
        safe_rel = os.path.normpath(rel).replace('\\', '/').lstrip('/')
        if safe_rel.startswith('..'):
            return self._send({'ok': False, 'error': 'forbidden'}, 403)
        fpath = os.path.join(STATIC_DIR, safe_rel)
        if not os.path.abspath(fpath).startswith(os.path.abspath(STATIC_DIR)):
            return self._send({'ok': False, 'error': 'forbidden'}, 403)
        if not os.path.isfile(fpath):
            # 🔍 index.html 이 없으면 흔한 대체 이름을 자동으로 찾아서 서빙
            #    (index_fixed.html, index (1).html 등으로 업로드해도 게임이 뜨게)
            if safe_rel == 'index.html':
                cands = ['index_fixed.html', 'index(1).html', 'index (1).html',
                         'index.htm', 'game.html', 'main.html']
                found = None
                for cand in cands:
                    p2 = os.path.join(STATIC_DIR, cand)
                    if os.path.isfile(p2):
                        found = p2; break
                if not found:
                    try:  # 폴더 안의 아무 .html 이라도 찾기
                        for fn in sorted(os.listdir(STATIC_DIR)):
                            if fn.lower().endswith('.html'):
                                found = os.path.join(STATIC_DIR, fn); break
                    except Exception:
                        pass
                if found:
                    fpath = found
                else:
                    return self._send({'ok': False, 'error': 'not_found',
                                        'hint': 'server.py 와 같은 폴더에 index.html 을 두세요.'}, 404)
            else:
                return self._send({'ok': False, 'error': 'not_found',
                                    'hint': 'server.py 와 같은 폴더에 index.html 을 두세요.'}, 404)
        ext = os.path.splitext(fpath)[1].lower()
        mime = _MIME.get(ext, 'application/octet-stream')
        ent = get_static_cached(fpath)
        if not ent:
            return self._send({'ok': False, 'error': 'read_failed'}, 500)
        data = ent['raw']
        # 🔗 HTML 파일이면 <head>에 canonical 태그 자동 삽입 (이미 있으면 건너뜀)
        if ext == '.html' and b'rel="canonical"' not in data and b"rel='canonical'" not in data:
            try:
                if b'<head>' in data:
                    data = data.replace(b'<head>', b'<head>\n    ' + CANONICAL_TAG, 1)
                elif b'<meta charset' in data:
                    idx = data.find(b'<meta charset')
                    end = data.find(b'>', idx)
                    if end != -1:
                        data = data[:end+1] + b'\n    ' + CANONICAL_TAG + data[end+1:]
            except Exception:
                pass
        # ⚡ 클라이언트가 gzip을 지원하면 압축본 전송 (전송량 ~75% 절감 → 초고속 로딩)
        use_gz = False
        if ent['gz'] and data is ent['raw']:
            ae = (self.headers.get('Accept-Encoding') or '')
            if 'gzip' in ae:
                data = ent['gz']; use_gz = True
        elif ext == '.html':
            ae = (self.headers.get('Accept-Encoding') or '')
            if 'gzip' in ae:
                try:
                    data = _gzip.compress(data, 6); use_gz = True
                except Exception:
                    pass
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Access-Control-Allow-Origin', '*')
        if use_gz:
            self.send_header('Content-Encoding', 'gzip')
        self.send_header('Cache-Control', 'no-cache')   # 항상 최신 확인(캐시는 서버 RAM이 담당)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urlparse(self.path)
        if not rate_ok(self.client_address[0], u.path):
            return self._send({'ok': False, 'error': 'rate_limited'}, 429)
        if u.path == '/chat/poll':   # ⚡ 채팅 롱폴 (전역 락 밖에서 대기)
            q = parse_qs(u.query)
            room = (q.get('room') or ['global'])[0][:40]
            since = int((q.get('since') or [0])[0] or 0)
            wait = int((q.get('wait') or [15])[0] or 15)
            try:
                return self._send(chat_poll(room, since, wait))
            except Exception as e:
                return self._send({'ok': False, 'error': str(e)}, 500)
        if u.path == '/sw.js':   # 📴 오프라인 캐시용 서비스워커
            self.send_response(200)
            self.send_header('Content-Type', 'application/javascript; charset=utf-8')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Cache-Control', 'no-cache')
            self.send_header('Content-Length', str(len(SW_JS)))
            self.end_headers()
            self.wfile.write(SW_JS)
            return
        if u.path not in API_PATHS:
            return self._serve_static(u.path)
        try:
            self._send(api(u.path, parse_qs(u.query), {}))
        except Exception as e:
            self._send({'ok': False, 'error': str(e)}, 500)

    def do_POST(self):
        u = urlparse(self.path)
        if not rate_ok(self.client_address[0], u.path):
            return self._send({'ok': False, 'error': 'rate_limited'}, 429)
        try:
            n = int(self.headers.get('Content-Length') or 0)
            body = json.loads(self.rfile.read(n) or b'{}') if n else {}
        except Exception:
            body = {}
        if u.path == '/chat/poll':   # ⚡ 채팅 롱폴 (전역 락 밖에서 대기)
            try:
                room = str(body.get('room') or 'global')[:40]
                since = int(body.get('since') or 0)
                wait = int(body.get('wait') or 15)
                return self._send(chat_poll(room, since, wait))
            except Exception as e:
                return self._send({'ok': False, 'error': str(e)}, 500)
        try:
            self._send(api(u.path, parse_qs(u.query), body))
        except Exception as e:
            self._send({'ok': False, 'error': str(e)}, 500)

    def log_message(self, *a):
        pass  # 콘솔 조용히


class ThreadedHTTP(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True   # 재시작 시 'Address in use' (TIME_WAIT) 방지


def start_background():
    """게임 런처에서 서버를 백그라운드로 띄울 때 사용"""
    init_db()
    srv = ThreadedHTTP((HOST, PORT), Handler)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    return srv


if __name__ == '__main__':
    init_db()
    do_backup()   # 시작 직후 1회 백업
    threading.Thread(target=backup_loop, daemon=True).start()
    _p('=' * 52)
    _p('[SERVER] 차원 균열의 만물상 - 온라인 서버')
    _p(f'   바인딩 : {HOST}:{PORT}')
    _p(f'   DB     : {DB_PATH}')
    _p(f'   정적파일: {STATIC_DIR} (index.html 을 여기 두세요)')
    _p(f'   속도제한: IP당 10초 {RATE_LIMIT}회')
    _p(f'   자동백업: {BACKUP_DIR} (30분마다, 최근 {BACKUP_KEEP}개 보관)')
    try:
        import socket as _sk
        s = _sk.socket(_sk.AF_INET, _sk.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        _p(f'   내부 IP : http://{s.getsockname()[0]}:{PORT}')
        s.close()
    except Exception:
        pass
    _p('   같은 PC : http://127.0.0.1:%d  (브라우저로 열면 게임이 바로 실행됩니다)' % PORT)
    _p('=' * 52)
    _p('종료하려면 Ctrl+C')
    while True:
        try:
            ThreadedHTTP((HOST, PORT), Handler).serve_forever()
        except KeyboardInterrupt:
            _p('\n서버를 종료합니다.')
            break
        except OSError as e:
            if getattr(e, 'errno', None) == 98:
                _p('')
                _p('[오류] 포트 %d 를 이미 다른 프로세스가 사용 중입니다!' % PORT)
                _p('  이전에 켜둔 서버가 아직 실행 중일 가능성이 큽니다.')
                _p('  아래 명령으로 종료 후 다시 실행하세요:')
                _p('    fuser -k %d/tcp' % PORT)
                _p('  또는:')
                _p('    pkill -f server.py')
            else:
                raise
            break
        except Exception as e:
            _p('[서버 오류] %r — 3초 후 자동 재시작' % (e,))
            time.sleep(3)

