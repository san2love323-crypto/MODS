import discord
from discord.ext import commands, tasks
import os
from datetime import datetime
import json
import aiohttp

# 봇 설정
intents = discord.Intents.default()
intents.message_content = True
intents.members = True
intents.moderation = True

bot = commands.Bot(command_prefix='!', intents=intents)

# ============ 설정 파일 ============
CONFIG_FILE = "bot_config.json"

def load_config():
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            return json.load(f)
    return {
        "TOKEN": " ",
        "guilds": {},
        "warnings": {},
        "muted_users": {}
    }

def save_config(config):
    with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
        json.dump(config, f, indent=4, ensure_ascii=False)

config = load_config()

# ============ 이벤트 ============
@bot.event
async def on_ready():
    print(f'✅ {bot.user}로 로그인했습니다!')
    print(f'📊 {len(bot.guilds)}개 서버에 참여 중입니다')
    await bot.change_presence(activity=discord.Activity(type=discord.ActivityType.listening, name="!help"))

@bot.event
async def on_member_join(member):
    """새 멤버 환영 메시지 및 자동 역할"""
    guild_id = str(member.guild.id)
    
    if guild_id not in config["guilds"]:
        config["guilds"][guild_id] = {
            "welcome_channel": None,
            "auto_role": None,
            "log_channel": None,
            "moderation_log": None
        }
    
    guild_config = config["guilds"][guild_id]
    
    # 환영 메시지
    if guild_config.get("welcome_channel"):
        welcome_channel = bot.get_channel(int(guild_config["welcome_channel"]))
        if welcome_channel:
            embed = discord.Embed(
                title=f"👋 {member.name}님을 환영합니다!",
                description=f"{member.mention}님이 서버에 입장했습니다.",
                color=discord.Color.green(),
                timestamp=datetime.now()
            )
            embed.set_thumbnail(url=member.avatar)
            await welcome_channel.send(embed=embed)
    
    # 자동 역할 할당
    if guild_config.get("auto_role"):
        auto_role = member.guild.get_role(int(guild_config["auto_role"]))
        if auto_role:
            try:
                await member.add_roles(auto_role)
            except Exception as e:
                print(f"역할 할당 실패: {e}")

@bot.event
async def on_member_remove(member):
    """멤버 퇴장 로깅"""
    guild_id = str(member.guild.id)
    if guild_id in config["guilds"]:
        log_channel_id = config["guilds"][guild_id].get("log_channel")
        if log_channel_id:
            log_channel = bot.get_channel(int(log_channel_id))
            if log_channel:
                embed = discord.Embed(
                    title="👤 멤버 퇴장",
                    description=f"{member.mention} ({member.name}#{member.discriminator})",
                    color=discord.Color.red(),
                    timestamp=datetime.now()
                )
                await log_channel.send(embed=embed)

# ============ 모더레이션 커맨드 ============

@bot.command(name='kick', description='사용자를 킥합니다')
@commands.has_permissions(kick_members=True)
async def kick(ctx, member: discord.Member, *, reason="사유 없음"):
    """사용자를 서버에서 추방합니다"""
    if member.top_role >= ctx.author.top_role:
        embed = discord.Embed(
            title="❌ 오류",
            description="같거나 높은 권한의 사용자는 킥할 수 없습니다.",
            color=discord.Color.red()
        )
        return await ctx.send(embed=embed)
    
    try:
        await member.kick(reason=reason)
        embed = discord.Embed(
            title="👢 킥 완료",
            description=f"{member.mention}이(가) 킥되었습니다.",
            color=discord.Color.orange(),
            timestamp=datetime.now()
        )
        embed.add_field(name="사유", value=reason, inline=False)
        embed.add_field(name="실행자", value=ctx.author.mention, inline=False)
        await ctx.send(embed=embed)
        
        # 로깅
        await log_moderation(ctx.guild, f"킥: {member} - {reason}", ctx.author)
    except Exception as e:
        await ctx.send(f"❌ 오류: {e}")

@bot.command(name='ban', description='사용자를 밴합니다')
@commands.has_permissions(ban_members=True)
async def ban(ctx, member: discord.Member, *, reason="사유 없음"):
    """사용자를 서버에서 영구 추방합니다"""
    if member.top_role >= ctx.author.top_role:
        embed = discord.Embed(
            title="❌ 오류",
            description="같거나 높은 권한의 사용자는 밴할 수 없습니다.",
            color=discord.Color.red()
        )
        return await ctx.send(embed=embed)
    
    try:
        await member.ban(reason=reason)
        embed = discord.Embed(
            title="🔨 밴 완료",
            description=f"{member.mention}이(가) 밴되었습니다.",
            color=discord.Color.dark_red(),
            timestamp=datetime.now()
        )
        embed.add_field(name="사유", value=reason, inline=False)
        embed.add_field(name="실행자", value=ctx.author.mention, inline=False)
        await ctx.send(embed=embed)
        
        # 로깅
        await log_moderation(ctx.guild, f"밴: {member} - {reason}", ctx.author)
    except Exception as e:
        await ctx.send(f"❌ 오류: {e}")

@bot.command(name='mute', description='사용자를 뮤트합니다')
@commands.has_permissions(moderate_members=True)
async def mute(ctx, member: discord.Member, duration_seconds: int = 3600, *, reason="사유 없음"):
    """사용자를 지정된 시간동안 뮤트합니다"""
    if member.top_role >= ctx.author.top_role:
        embed = discord.Embed(
            title="❌ 오류",
            description="같거나 높은 권한의 사용자는 뮤트할 수 없습니다.",
            color=discord.Color.red()
        )
        return await ctx.send(embed=embed)
    
    try:
        from datetime import timedelta
        await member.timeout(timedelta(seconds=duration_seconds), reason=reason)
        
        hours = duration_seconds // 3600
        minutes = (duration_seconds % 3600) // 60
        
        embed = discord.Embed(
            title="🔇 뮤트 완료",
            description=f"{member.mention}이(가) 뮤트되었습니다.",
            color=discord.Color.gold(),
            timestamp=datetime.now()
        )
        embed.add_field(name="기간", value=f"{hours}시간 {minutes}분", inline=False)
        embed.add_field(name="사유", value=reason, inline=False)
        embed.add_field(name="실행자", value=ctx.author.mention, inline=False)
        await ctx.send(embed=embed)
        
        # 로깅
        await log_moderation(ctx.guild, f"뮤트: {member} ({hours}h {minutes}m) - {reason}", ctx.author)
    except Exception as e:
        await ctx.send(f"❌ 오류: {e}")

@bot.command(name='unmute', description='사용자의 뮤트를 해제합니다')
@commands.has_permissions(moderate_members=True)
async def unmute(ctx, member: discord.Member):
    """사용자의 뮤트를 해제합니다"""
    try:
        await member.timeout(None)
        embed = discord.Embed(
            title="🔊 뮤트 해제",
            description=f"{member.mention}의 뮤트가 해제되었습니다.",
            color=discord.Color.green(),
            timestamp=datetime.now()
        )
        embed.add_field(name="실행자", value=ctx.author.mention, inline=False)
        await ctx.send(embed=embed)
        
        await log_moderation(ctx.guild, f"뮤트 해제: {member}", ctx.author)
    except Exception as e:
        await ctx.send(f"❌ 오류: {e}")

@bot.command(name='warn', description='사용자에게 경고를 줍니다')
@commands.has_permissions(moderate_members=True)
async def warn(ctx, member: discord.Member, *, reason="사유 없음"):
    """사용자에게 경고를 부여합니다"""
    guild_id = str(ctx.guild.id)
    user_id = str(member.id)
    
    if "warnings" not in config:
        config["warnings"] = {}
    if guild_id not in config["warnings"]:
        config["warnings"][guild_id] = {}
    if user_id not in config["warnings"][guild_id]:
        config["warnings"][guild_id][user_id] = []
    
    config["warnings"][guild_id][user_id].append({
        "reason": reason,
        "timestamp": datetime.now().isoformat(),
        "moderator": str(ctx.author.id)
    })
    save_config(config)
    
    warn_count = len(config["warnings"][guild_id][user_id])
    
    embed = discord.Embed(
        title="⚠️ 경고 부여",
        description=f"{member.mention}이(가) 경고를 받았습니다.",
        color=discord.Color.yellow(),
        timestamp=datetime.now()
    )
    embed.add_field(name="사유", value=reason, inline=False)
    embed.add_field(name="누적 경고", value=f"{warn_count}/3", inline=False)
    embed.add_field(name="실행자", value=ctx.author.mention, inline=False)
    await ctx.send(embed=embed)
    
    # 자동 처벌
    if warn_count >= 3:
        await member.ban(reason="누적 경고 3회")
        embed = discord.Embed(
            title="🔨 자동 밴",
            description=f"{member.mention}이(가) 누적 경고 3회로 자동 밴되었습니다.",
            color=discord.Color.dark_red()
        )
        await ctx.send(embed=embed)
    
    await log_moderation(ctx.guild, f"경고: {member} ({warn_count}/3) - {reason}", ctx.author)

@bot.command(name='warnings', description='사용자의 경고 목록을 봅니다')
@commands.has_permissions(moderate_members=True)
async def warnings(ctx, member: discord.Member):
    """사용자의 경고 목록을 표시합니다"""
    guild_id = str(ctx.guild.id)
    user_id = str(member.id)
    
    if guild_id not in config.get("warnings", {}) or user_id not in config["warnings"][guild_id]:
        embed = discord.Embed(
            title=f"{member.name}의 경고",
            description="경고가 없습니다.",
            color=discord.Color.green()
        )
        return await ctx.send(embed=embed)
    
    warnings_list = config["warnings"][guild_id][user_id]
    
    embed = discord.Embed(
        title=f"{member.name}의 경고 ({len(warnings_list)}/3)",
        color=discord.Color.yellow()
    )
    
    for i, warning in enumerate(warnings_list, 1):
        embed.add_field(
            name=f"경고 #{i}",
            value=f"**사유:** {warning['reason']}\n**시간:** {warning['timestamp'][:10]}",
            inline=False
        )
    
    await ctx.send(embed=embed)

@bot.command(name='clear', description='메시지를 삭제합니다')
@commands.has_permissions(manage_messages=True)
async def clear(ctx, amount: int = 5):
    """지정된 개수의 메시지를 삭제합니다"""
    if amount > 100:
        await ctx.send("❌ 한 번에 100개 이하의 메시지만 삭제할 수 있습니다.")
        return
    
    deleted = await ctx.channel.purge(limit=amount)
    embed = discord.Embed(
        title="🧹 메시지 삭제",
        description=f"{len(deleted)}개의 메시지가 삭제되었습니다.",
        color=discord.Color.blue(),
        timestamp=datetime.now()
    )
    embed.add_field(name="채널", value=ctx.channel.mention, inline=False)
    embed.add_field(name="실행자", value=ctx.author.mention, inline=False)
    await ctx.send(embed=embed, delete_after=5)
    
    await log_moderation(ctx.guild, f"메시지 삭제: {len(deleted)}개", ctx.author)

# ============ 설정 커맨드 ============

@bot.command(name='setwelcome', description='환영 채널을 설정합니다')
@commands.has_permissions(manage_guild=True)
async def setwelcome(ctx, channel: discord.TextChannel = None):
    """새 멤버를 환영할 채널을 설정합니다"""
    guild_id = str(ctx.guild.id)
    
    if guild_id not in config["guilds"]:
        config["guilds"][guild_id] = {
            "welcome_channel": None,
            "auto_role": None,
            "log_channel": None,
            "moderation_log": None
        }
    
    if channel is None:
        config["guilds"][guild_id]["welcome_channel"] = None
        embed = discord.Embed(
            title="✅ 환영 채널 비활성화",
            color=discord.Color.green()
        )
    else:
        config["guilds"][guild_id]["welcome_channel"] = str(channel.id)
        embed = discord.Embed(
            title="✅ 환영 채널 설정 완료",
            description=f"환영 채널: {channel.mention}",
            color=discord.Color.green()
        )
    
    save_config(config)
    await ctx.send(embed=embed)

@bot.command(name='setautorole', description='자동 역할을 설정합니다')
@commands.has_permissions(manage_guild=True)
async def setautorole(ctx, role: discord.Role = None):
    """새 멤버에게 자동으로 부여할 역할을 설정합니다"""
    guild_id = str(ctx.guild.id)
    
    if guild_id not in config["guilds"]:
        config["guilds"][guild_id] = {
            "welcome_channel": None,
            "auto_role": None,
            "log_channel": None,
            "moderation_log": None
        }
    
    if role is None:
        config["guilds"][guild_id]["auto_role"] = None
        embed = discord.Embed(
            title="✅ 자동 역할 비활성화",
            color=discord.Color.green()
        )
    else:
        config["guilds"][guild_id]["auto_role"] = str(role.id)
        embed = discord.Embed(
            title="✅ 자동 역할 설정 완료",
            description=f"자동 역할: {role.mention}",
            color=discord.Color.green()
        )
    
    save_config(config)
    await ctx.send(embed=embed)

@bot.command(name='setlogchannel', description='로그 채널을 설정합니다')
@commands.has_permissions(manage_guild=True)
async def setlogchannel(ctx, channel: discord.TextChannel = None):
    """멤버 참여/퇴장 로그를 받을 채널을 설정합니다"""
    guild_id = str(ctx.guild.id)
    
    if guild_id not in config["guilds"]:
        config["guilds"][guild_id] = {
            "welcome_channel": None,
            "auto_role": None,
            "log_channel": None,
            "moderation_log": None
        }
    
    if channel is None:
        config["guilds"][guild_id]["log_channel"] = None
        embed = discord.Embed(
            title="✅ 로그 채널 비활성화",
            color=discord.Color.green()
        )
    else:
        config["guilds"][guild_id]["log_channel"] = str(channel.id)
        embed = discord.Embed(
            title="✅ 로그 채널 설정 완료",
            description=f"로그 채널: {channel.mention}",
            color=discord.Color.green()
        )
    
    save_config(config)
    await ctx.send(embed=embed)

@bot.command(name='setmodlog', description='중재 로그 채널을 설정합니다')
@commands.has_permissions(manage_guild=True)
async def setmodlog(ctx, channel: discord.TextChannel = None):
    """중재 기록을 받을 채널을 설정합니다"""
    guild_id = str(ctx.guild.id)
    
    if guild_id not in config["guilds"]:
        config["guilds"][guild_id] = {
            "welcome_channel": None,
            "auto_role": None,
            "log_channel": None,
            "moderation_log": None
        }
    
    if channel is None:
        config["guilds"][guild_id]["moderation_log"] = None
        embed = discord.Embed(
            title="✅ 중재 로그 비활성화",
            color=discord.Color.green()
        )
    else:
        config["guilds"][guild_id]["moderation_log"] = str(channel.id)
        embed = discord.Embed(
            title="✅ 중재 로그 채널 설정 완료",
            description=f"중재 로그 채널: {channel.mention}",
            color=discord.Color.green()
        )
    
    save_config(config)
    await ctx.send(embed=embed)

# ============ 유틸리티 커맨드 ============

@bot.command(name='userinfo', description='사용자 정보를 봅니다')
async def userinfo(ctx, member: discord.Member = None):
    """사용자의 정보를 표시합니다"""
    if member is None:
        member = ctx.author
    
    embed = discord.Embed(
        title=f"{member.name}의 정보",
        color=member.color,
        timestamp=datetime.now()
    )
    embed.set_thumbnail(url=member.avatar)
    embed.add_field(name="사용자명", value=f"{member.name}#{member.discriminator}", inline=False)
    embed.add_field(name="ID", value=member.id, inline=False)
    embed.add_field(name="가입일", value=member.joined_at.strftime("%Y-%m-%d %H:%M:%S"), inline=False)
    embed.add_field(name="계정생성일", value=member.created_at.strftime("%Y-%m-%d %H:%M:%S"), inline=False)
    
    roles = [role.mention for role in member.roles if role != ctx.guild.default_role]
    if roles:
        embed.add_field(name="역할", value=", ".join(roles[:10]), inline=False)
    
    embed.add_field(name="최고 역할", value=member.top_role.mention, inline=False)
    
    await ctx.send(embed=embed)

@bot.command(name='serverinfo', description='서버 정보를 봅니다')
async def serverinfo(ctx):
    """서버의 정보를 표시합니다"""
    guild = ctx.guild
    
    embed = discord.Embed(
        title=f"{guild.name}의 정보",
        color=discord.Color.blue(),
        timestamp=datetime.now()
    )
    embed.set_thumbnail(url=guild.icon)
    embed.add_field(name="서버명", value=guild.name, inline=False)
    embed.add_field(name="서버 ID", value=guild.id, inline=False)
    embed.add_field(name="소유자", value=guild.owner.mention, inline=False)
    embed.add_field(name="멤버 수", value=guild.member_count, inline=False)
    embed.add_field(name="채널 수", value=len(guild.channels), inline=False)
    embed.add_field(name="역할 수", value=len(guild.roles), inline=False)
    embed.add_field(name="생성일", value=guild.created_at.strftime("%Y-%m-%d %H:%M:%S"), inline=False)
    embed.add_field(name="인증 레벨", value=guild.verification_level, inline=False)
    
    await ctx.send(embed=embed)

@bot.command(name='ping', description='봇의 응답시간을 봅니다')
async def ping(ctx):
    """봇의 응답 시간을 표시합니다"""
    embed = discord.Embed(
        title="🏓 Pong!",
        description=f"응답 시간: {round(bot.latency * 1000)}ms",
        color=discord.Color.green()
    )
    await ctx.send(embed=embed)

@bot.command(name='help', description='도움말을 봅니다')
async def help(ctx, command_name: str = None):
    """도움말을 표시합니다"""
    if command_name:
        command = bot.get_command(command_name)
        if command:
            embed = discord.Embed(
                title=f"도움말: {command.name}",
                description=command.description or "설명 없음",
                color=discord.Color.blue()
            )
            await ctx.send(embed=embed)
        else:
            await ctx.send(f"❌ '{command_name}' 커맨드를 찾을 수 없습니다.")
    else:
        embed = discord.Embed(
            title="🤖 봇 커맨드 목록",
            description="모든 커맨드를 표시합니다.",
            color=discord.Color.blue()
        )
        
        # 모더레이션
        embed.add_field(
            name="🔨 모더레이션",
            value="""
`!kick <사용자> [사유]` - 사용자 추방
`!ban <사용자> [사유]` - 사용자 밴
`!mute <사용자> [시간] [사유]` - 사용자 뮤트
`!unmute <사용자>` - 뮤트 해제
`!warn <사용자> [사유]` - 경고 부여
`!warnings <사용자>` - 경고 목록 조회
`!clear [개수]` - 메시지 삭제
            """,
            inline=False
        )
        
        # 설정
        embed.add_field(
            name="⚙️ 설정",
            value="""
`!setwelcome [채널]` - 환영 채널 설정
`!setautorole [역할]` - 자동 역할 설정
`!setlogchannel [채널]` - 로그 채널 설정
`!setmodlog [채널]` - 중재 로그 채널 설정
            """,
            inline=False
        )
        
        # 유틸리티
        embed.add_field(
            name="🛠️ 유틸리티",
            value="""
`!userinfo [사용자]` - 사용자 정보
`!serverinfo` - 서버 정보
`!ping` - 응답 시간 확인
`!help [커맨드]` - 도움말
            """,
            inline=False
        )
        
        await ctx.send(embed=embed)

# ============ 헬퍼 함수 ============

async def log_moderation(guild, action, moderator):
    """중재 기록을 로그 채널에 저장합니다"""
    guild_id = str(guild.id)
    if guild_id in config["guilds"]:
        mod_log_id = config["guilds"][guild_id].get("moderation_log")
        if mod_log_id:
            try:
                mod_log_channel = bot.get_channel(int(mod_log_id))
                if mod_log_channel:
                    embed = discord.Embed(
                        title="📋 중재 기록",
                        description=action,
                        color=discord.Color.orange(),
                        timestamp=datetime.now()
                    )
                    embed.add_field(name="중재자", value=moderator.mention, inline=False)
                    await mod_log_channel.send(embed=embed)
            except Exception as e:
                print(f"로깅 오류: {e}")

# ============ 에러 핸들러 ============

@bot.event
async def on_command_error(ctx, error):
    """커맨드 에러를 처리합니다"""
    if isinstance(error, commands.MissingPermissions):
        embed = discord.Embed(
            title="❌ 권한 부족",
            description="이 커맨드를 사용할 권한이 없습니다.",
            color=discord.Color.red()
        )
        await ctx.send(embed=embed)
    elif isinstance(error, commands.MissingRequiredArgument):
        embed = discord.Embed(
            title="❌ 인자 부족",
            description=f"필수 인자가 없습니다.",
            color=discord.Color.red()
        )
        await ctx.send(embed=embed)
    elif isinstance(error, commands.BadArgument):
        embed = discord.Embed(
            title="❌ 잘못된 인자",
            description="인자 형식이 잘못되었습니다.",
            color=discord.Color.red()
        )
        await ctx.send(embed=embed)
    else:
        print(f"에러: {error}")



# ════════════════════════════════════════════════════════════════
# 🎰 경제 확장팩: 주식 · 도박 · 일일 퀴즈 · 코인 상점 · 게임 환전
# ════════════════════════════════════════════════════════════════
import random as _rd
import asyncio as _aio
import time as _time

ECON_FILE = "bot_economy.json"

def _eload():
    if os.path.exists(ECON_FILE):
        try:
            with open(ECON_FILE, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception:
            pass
    return {"users": {}, "stocks": None, "stock_ts": 0}

def _esave(d):
    with open(ECON_FILE, 'w', encoding='utf-8') as f:
        json.dump(d, f, indent=2, ensure_ascii=False)

ECON = _eload()

def _u(uid):
    uid = str(uid)
    if uid not in ECON["users"]:
        ECON["users"][uid] = {"money": 5000, "coins": 0, "stocks": {},
                              "quiz_day": "", "nick": ""}
    return ECON["users"][uid]

def _fmt(n):
    return f"{int(n):,}"

# ── 게임 서버 연동 설정 (bot_config.json 에서 관리) ──
def _game_cfg():
    return (config.get("GAME_SERVER") or "http://127.0.0.1:8777",
            config.get("GAME_ADMIN_PASS") or "")

async def _game_mail(nick, subject, body, gold=0):
    """게임 서버 우편으로 골드/알림 발송"""
    base, _ = _game_cfg()
    try:
        async with aiohttp.ClientSession() as s:
            async with s.post(base + "/mail/send", json={
                "to": nick, "nick": "디스코드봇", "subject": subject,
                "body": body, "gold": int(gold)
            }, timeout=aiohttp.ClientTimeout(total=8)) as r:
                d = await r.json()
                return bool(d.get("ok"))
    except Exception as e:
        print("게임 연동 실패:", e)
        return False

# ── 📈 주식 시장 (봇 내 가상 주식, 5분마다 변동) ──
STOCK_DEFS = [
    ("RIFT", "균열에너지", 1000), ("MANM", "만물상홀딩스", 500),
    ("GOBL", "고블린광업", 250),  ("DRGN", "드래곤항공", 2000),
    ("SLIM", "슬라임바이오", 100), ("GOLD", "골드뱅크", 800),
]

def _stocks():
    now = _time.time()
    if ECON["stocks"] is None:
        ECON["stocks"] = {sym: price for sym, _, price in STOCK_DEFS}
        ECON["stock_ts"] = now
        _esave(ECON)
    # 5분마다 가격 변동 (경과한 틱만큼 적용, 최대 24틱)
    ticks = min(24, int((now - ECON.get("stock_ts", now)) // 300))
    if ticks > 0:
        for _ in range(ticks):
            for sym in ECON["stocks"]:
                chg = _rd.uniform(-0.12, 0.13)
                base = dict((s, p) for s, _, p in STOCK_DEFS)[sym]
                np_ = ECON["stocks"][sym] * (1 + chg)
                ECON["stocks"][sym] = max(base * 0.2, min(base * 8, round(np_, 1)))
        ECON["stock_ts"] = now
        _esave(ECON)
    return ECON["stocks"]

@bot.command(name='주식', description='주식 시세를 봅니다')
async def stock_list(ctx):
    prices = _stocks()
    u = _u(ctx.author.id)
    emb = discord.Embed(title="📈 균열 증권거래소", color=discord.Color.green(),
                        description="5분마다 시세가 변동합니다\n`!매수 종목 수량` · `!매도 종목 수량`")
    for sym, name, base in STOCK_DEFS:
        p = prices[sym]
        pct = (p / base - 1) * 100
        arrow = "🔺" if pct >= 0 else "🔻"
        own = u["stocks"].get(sym, 0)
        emb.add_field(name=f"{sym} · {name}",
                      value=f"💰 {_fmt(p)}원 {arrow}{abs(pct):.1f}%" + (f"\n보유 {own}주" if own else ""),
                      inline=True)
    emb.set_footer(text=f"내 잔고: {_fmt(u['money'])}원 · 💎 {u['coins']} 코인")
    await ctx.send(embed=emb)

@bot.command(name='매수', description='주식을 삽니다')
async def stock_buy(ctx, sym: str = None, qty: int = 1):
    if not sym:
        return await ctx.send("사용법: `!매수 RIFT 10`")
    sym = sym.upper()
    prices = _stocks()
    if sym not in prices:
        return await ctx.send("❌ 없는 종목입니다. `!주식`으로 확인하세요.")
    qty = max(1, min(10000, qty))
    u = _u(ctx.author.id)
    cost = int(prices[sym] * qty)
    if u["money"] < cost:
        return await ctx.send(f"❌ 잔고 부족! 필요: {_fmt(cost)}원 / 보유: {_fmt(u['money'])}원")
    u["money"] -= cost
    u["stocks"][sym] = u["stocks"].get(sym, 0) + qty
    _esave(ECON)
    await ctx.send(f"✅ **{sym} {qty}주** 매수 완료! (-{_fmt(cost)}원 · 잔고 {_fmt(u['money'])}원)")

@bot.command(name='매도', description='주식을 팝니다')
async def stock_sell(ctx, sym: str = None, qty: int = 1):
    if not sym:
        return await ctx.send("사용법: `!매도 RIFT 10`")
    sym = sym.upper()
    prices = _stocks()
    u = _u(ctx.author.id)
    own = u["stocks"].get(sym, 0)
    if own <= 0:
        return await ctx.send("❌ 보유하지 않은 종목입니다.")
    qty = max(1, min(own, qty))
    gain = int(prices[sym] * qty)
    u["stocks"][sym] = own - qty
    if u["stocks"][sym] == 0:
        del u["stocks"][sym]
    u["money"] += gain
    _esave(ECON)
    await ctx.send(f"✅ **{sym} {qty}주** 매도 완료! (+{_fmt(gain)}원 · 잔고 {_fmt(u['money'])}원)")

# ── 🎰 도박 ──
@bot.command(name='도박', description='돈을 걸고 도박합니다')
async def gamble(ctx, amount: int = None):
    if not amount or amount < 100:
        return await ctx.send("사용법: `!도박 1000` (최소 100원)")
    u = _u(ctx.author.id)
    if u["money"] < amount:
        return await ctx.send(f"❌ 잔고 부족! 보유: {_fmt(u['money'])}원")
    u["money"] -= amount
    r = _rd.random()
    if r < 0.02:      # 2% 잭팟 5배
        win = amount * 5
        msg = f"🎰🎰🎰 **잭팟!!** +{_fmt(win)}원 (5배)"
    elif r < 0.12:    # 10% 2배
        win = amount * 2
        msg = f"🎉 **대박!** +{_fmt(win)}원 (2배)"
    elif r < 0.47:    # 35% 1.5배
        win = int(amount * 1.5)
        msg = f"✨ 승리! +{_fmt(win)}원 (1.5배)"
    else:             # 53% 꽝
        win = 0
        msg = f"💸 꽝... -{_fmt(amount)}원"
    u["money"] += win
    _esave(ECON)
    emb = discord.Embed(title="🎰 균열 카지노", description=msg, color=discord.Color.gold() if win else discord.Color.dark_grey())
    emb.set_footer(text=f"잔고: {_fmt(u['money'])}원")
    await ctx.send(embed=emb)

@bot.command(name='슬롯', description='슬롯머신을 돌립니다')
async def slots(ctx, amount: int = None):
    if not amount or amount < 100:
        return await ctx.send("사용법: `!슬롯 500`")
    u = _u(ctx.author.id)
    if u["money"] < amount:
        return await ctx.send(f"❌ 잔고 부족!")
    u["money"] -= amount
    icons = ["🍒", "🍋", "💎", "7️⃣", "🔔"]
    roll = [_rd.choice(icons) for _ in range(3)]
    line = " | ".join(roll)
    if roll[0] == roll[1] == roll[2]:
        mult = 10 if roll[0] == "7️⃣" else 5
        win = amount * mult
        msg = f"**[{line}]**\n🎉 트리플! +{_fmt(win)}원 ({mult}배)"
    elif roll[0] == roll[1] or roll[1] == roll[2] or roll[0] == roll[2]:
        win = amount * 2
        msg = f"**[{line}]**\n✨ 더블! +{_fmt(win)}원 (2배)"
    else:
        win = 0
        msg = f"**[{line}]**\n💸 꽝..."
    u["money"] += win
    _esave(ECON)
    await ctx.send(embed=discord.Embed(title="🎰 슬롯머신", description=msg,
                                       color=discord.Color.gold() if win else discord.Color.dark_grey())
                   .set_footer(text=f"잔고: {_fmt(u['money'])}원"))

# ── 🧮 일일 퀴즈 (수학: 덧셈/뺄셈/곱셈 → 랜덤 코인) ──
_active_quiz = {}   # uid -> answer

@bot.command(name='퀴즈', description='일일 수학 퀴즈 (맞추면 랜덤 코인)')
async def daily_quiz(ctx):
    u = _u(ctx.author.id)
    today = datetime.now().strftime("%Y-%m-%d")
    if u["quiz_day"] == today:
        return await ctx.send("⏰ 오늘의 퀴즈는 이미 참여했습니다! 내일 다시 도전하세요.")
    op = _rd.choice(["+", "-", "×"])
    if op == "+":
        a, b = _rd.randint(10, 999), _rd.randint(10, 999); ans = a + b
    elif op == "-":
        a, b = _rd.randint(100, 999), _rd.randint(10, 99); ans = a - b
    else:
        a, b = _rd.randint(2, 30), _rd.randint(2, 30); ans = a * b
    _active_quiz[str(ctx.author.id)] = (ans, today)
    emb = discord.Embed(title="🧮 오늘의 수학 퀴즈!",
                        description=f"# `{a} {op} {b} = ?`\n\n30초 안에 **답만** 채팅에 입력하세요!",
                        color=discord.Color.blue())
    await ctx.send(embed=emb)

    def chk(m):
        return m.author.id == ctx.author.id and m.channel == ctx.channel and m.content.lstrip("-").isdigit()
    try:
        msg = await bot.wait_for("message", check=chk, timeout=30)
    except _aio.TimeoutError:
        _active_quiz.pop(str(ctx.author.id), None)
        return await ctx.send(f"⏰ 시간 초과! 정답은 **{ans}** 였습니다.")
    _active_quiz.pop(str(ctx.author.id), None)
    if int(msg.content) == ans:
        coins = _rd.choice([3, 5, 5, 8, 10, 15, 20])   # 랜덤 코인
        u["coins"] += coins
        u["quiz_day"] = today
        _esave(ECON)
        await ctx.send(embed=discord.Embed(
            title="🎉 정답!", description=f"💎 **코인 +{coins}** 획득! (보유 {u['coins']}코인)",
            color=discord.Color.gold()))
    else:
        u["quiz_day"] = today
        _esave(ECON)
        await ctx.send(f"❌ 오답! 정답은 **{ans}** 였습니다. 내일 다시 도전하세요!")

# ── 💰 지갑/일일보상 ──
@bot.command(name='지갑', description='내 잔고를 봅니다')
async def wallet(ctx):
    u = _u(ctx.author.id)
    prices = _stocks()
    stock_val = sum(int(prices.get(s, 0)) * q for s, q in u["stocks"].items())
    emb = discord.Embed(title=f"💰 {ctx.author.display_name}의 지갑", color=discord.Color.blurple())
    emb.add_field(name="현금", value=f"{_fmt(u['money'])}원", inline=True)
    emb.add_field(name="💎 코인", value=f"{u['coins']}개", inline=True)
    emb.add_field(name="📈 주식 평가액", value=f"{_fmt(stock_val)}원", inline=True)
    if u["nick"]:
        emb.set_footer(text=f"연동된 게임 닉네임: {u['nick']}")
    else:
        emb.set_footer(text="!연동 게임닉네임 으로 게임 계정을 연결하세요")
    await ctx.send(embed=emb)

@bot.command(name='출석', description='일일 보상을 받습니다')
async def daily(ctx):
    u = _u(ctx.author.id)
    today = datetime.now().strftime("%Y-%m-%d")
    if u.get("daily_day") == today:
        return await ctx.send("⏰ 오늘 출석은 이미 완료!")
    u["daily_day"] = today
    amt = _rd.randint(1000, 3000)
    u["money"] += amt
    _esave(ECON)
    await ctx.send(f"✅ 출석 완료! **+{_fmt(amt)}원** (잔고 {_fmt(u['money'])}원)")

# ── 🔗 게임 계정 연동 ──
@bot.command(name='연동', description='게임 닉네임을 연동합니다')
async def link_game(ctx, *, nick: str = None):
    if not nick:
        return await ctx.send("사용법: `!연동 내게임닉네임`")
    u = _u(ctx.author.id)
    u["nick"] = nick.strip()[:20]
    _esave(ECON)
    await ctx.send(f"🔗 게임 닉네임 **[{u['nick']}]** 연동 완료! 이제 환전할 수 있습니다.")

# ── 🏪 상점 GUI (버튼) ──
class ShopView(discord.ui.View):
    """💰 현금 상점: 도박/주식으로 번 돈 → 게임 골드 환전"""
    def __init__(self, uid):
        super().__init__(timeout=120)
        self.uid = uid

    async def _exchange_gold(self, itx, cost, gold):
        if str(itx.user.id) != str(self.uid):
            return await itx.response.send_message("본인 전용 상점입니다!", ephemeral=True)
        u = _u(itx.user.id)
        if not u["nick"]:
            return await itx.response.send_message("먼저 `!연동 게임닉네임`으로 게임 계정을 연결하세요!", ephemeral=True)
        if u["money"] < cost:
            return await itx.response.send_message(f"❌ 잔고 부족! ({_fmt(cost)}원 필요)", ephemeral=True)
        u["money"] -= cost
        _esave(ECON)
        ok = await _game_mail(u["nick"], "💱 디스코드 환전", f"디스코드에서 {_fmt(cost)}원을 환전했습니다!", gold=gold)
        if ok:
            await itx.response.send_message(f"✅ **게임 골드 {_fmt(gold)}G** 환전 완료! 게임 우편함을 확인하세요. (잔고 {_fmt(u['money'])}원)", ephemeral=False)
        else:
            u["money"] += cost
            _esave(ECON)
            await itx.response.send_message("❌ 게임 서버 연결 실패... 돈을 돌려드렸어요. 잠시 후 다시 시도!", ephemeral=True)

    @discord.ui.button(label="골드 10,000G (5,000원)", style=discord.ButtonStyle.primary, emoji="🪙")
    async def g1(self, itx, btn): await self._exchange_gold(itx, 5000, 10000)

    @discord.ui.button(label="골드 50,000G (20,000원)", style=discord.ButtonStyle.primary, emoji="💰")
    async def g2(self, itx, btn): await self._exchange_gold(itx, 20000, 50000)

    @discord.ui.button(label="골드 200,000G (60,000원)", style=discord.ButtonStyle.success, emoji="🏆")
    async def g3(self, itx, btn): await self._exchange_gold(itx, 60000, 200000)

class CoinShopView(discord.ui.View):
    """💎 코인 전용 샵: 퀴즈로 모은 코인 → 게임 코인 환전"""
    def __init__(self, uid):
        super().__init__(timeout=120)
        self.uid = uid

    async def _exchange_coin(self, itx, cost, game_coins):
        if str(itx.user.id) != str(self.uid):
            return await itx.response.send_message("본인 전용 상점입니다!", ephemeral=True)
        u = _u(itx.user.id)
        if not u["nick"]:
            return await itx.response.send_message("먼저 `!연동 게임닉네임`으로 게임 계정을 연결하세요!", ephemeral=True)
        if u["coins"] < cost:
            return await itx.response.send_message(f"❌ 코인 부족! (💎{cost} 필요 / 보유 💎{u['coins']})", ephemeral=True)
        u["coins"] -= cost
        _esave(ECON)
        # 게임 클라가 우편 제목의 [코인:N] 마커를 인식해 코인 지급
        ok = await _game_mail(u["nick"], f"[코인:{game_coins}] 💎 코인 교환권",
                              f"디스코드 코인 샵에서 교환한 게임 코인 {game_coins}개입니다! 우편 수령 시 자동 지급됩니다.", gold=0)
        if ok:
            await itx.response.send_message(f"✅ **게임 코인 💎{game_coins}개** 환전 완료! 게임 우편함에서 수령하세요. (남은 코인 💎{u['coins']})", ephemeral=False)
        else:
            u["coins"] += cost
            _esave(ECON)
            await itx.response.send_message("❌ 게임 서버 연결 실패... 코인을 돌려드렸어요.", ephemeral=True)

    @discord.ui.button(label="게임 코인 10개 (💎10)", style=discord.ButtonStyle.primary, emoji="💎")
    async def c1(self, itx, btn): await self._exchange_coin(itx, 10, 10)

    @discord.ui.button(label="게임 코인 60개 (💎50)", style=discord.ButtonStyle.primary, emoji="💠")
    async def c2(self, itx, btn): await self._exchange_coin(itx, 50, 60)

    @discord.ui.button(label="게임 코인 150개 (💎100)", style=discord.ButtonStyle.success, emoji="👑")
    async def c3(self, itx, btn): await self._exchange_coin(itx, 100, 150)

@bot.command(name='상점', description='현금 상점 (골드 환전)')
async def shop(ctx):
    u = _u(ctx.author.id)
    emb = discord.Embed(title="🏪 만물상 환전소",
                        description="🎰 도박·📈 주식으로 번 **돈**을 게임 **골드**로 환전!\n연동: " + (f"✅ {u['nick']}" if u["nick"] else "❌ `!연동 닉네임` 필요"),
                        color=discord.Color.gold())
    emb.set_footer(text=f"잔고: {_fmt(u['money'])}원")
    await ctx.send(embed=emb, view=ShopView(ctx.author.id))

@bot.command(name='코인샵', description='코인 전용 샵 (게임 코인 환전)')
async def coinshop(ctx):
    u = _u(ctx.author.id)
    emb = discord.Embed(title="💎 코인 전용 샵",
                        description="🧮 **일일 퀴즈**로 모은 코인을 게임 **💎 코인**으로 환전!\n연동: " + (f"✅ {u['nick']}" if u["nick"] else "❌ `!연동 닉네임` 필요"),
                        color=discord.Color.purple())
    emb.set_footer(text=f"보유: 💎 {u['coins']} 코인")
    await ctx.send(embed=emb, view=CoinShopView(ctx.author.id))

# ── 💱 골드 ↔ 코인 상호 환전 ──
@bot.command(name='환전', description='골드 ↔ 코인 상호 환전')
async def exchange(ctx, type_: str = None, amount: int = None):
    """
    사용법:
    `!환전 골드 1000` → 골드(현금) 1000원 → 코인으로 환전 (비율: 100원 = 1코인)
    `!환전 코인 10` → 코인 10개 → 골드(현금)로 환전 (비율: 1코인 = 100원)
    """
    if not type_ or not amount:
        emb = discord.Embed(
            title="💱 환전 안내",
            description="`!환전 골드 1000` - 1000원 → 코인으로 (비율: 100원 = 1💎)\n`!환전 코인 10` - 10💎 → 1000원으로",
            color=discord.Color.blurple()
        )
        return await ctx.send(embed=emb)
    
    type_ = type_.lower()
    u = _u(ctx.author.id)
    
    if type_ == "골드":
        # 골드(현금) → 코인 (100원 = 1코인)
        if amount < 100:
            return await ctx.send("❌ 최소 100원부터 환전 가능합니다!")
        if u["money"] < amount:
            return await ctx.send(f"❌ 골드 부족! 보유: {_fmt(u['money'])}원")
        
        coin_gain = amount // 100
        u["money"] -= amount
        u["coins"] += coin_gain
        _esave(ECON)
        
        emb = discord.Embed(
            title="💱 환전 완료!",
            description=f"💰 **{_fmt(amount)}원** → 💎 **{coin_gain}개** 변환됨!",
            color=discord.Color.gold()
        )
        emb.add_field(name="남은 골드", value=f"{_fmt(u['money'])}원", inline=True)
        emb.add_field(name="현재 코인", value=f"💎 {u['coins']}개", inline=True)
        await ctx.send(embed=emb)
    
    elif type_ == "코인":
        # 코인 → 골드(현금) (1코인 = 100원)
        if amount < 1:
            return await ctx.send("❌ 최소 1개부터 환전 가능합니다!")
        if u["coins"] < amount:
            return await ctx.send(f"❌ 코인 부족! 보유: 💎 {u['coins']}개")
        
        gold_gain = amount * 100
        u["coins"] -= amount
        u["money"] += gold_gain
        _esave(ECON)
        
        emb = discord.Embed(
            title="💱 환전 완료!",
            description=f"💎 **{amount}개** → 💰 **{_fmt(gold_gain)}원** 변환됨!",
            color=discord.Color.gold()
        )
        emb.add_field(name="현재 골드", value=f"{_fmt(u['money'])}원", inline=True)
        emb.add_field(name="남은 코인", value=f"💎 {u['coins']}개", inline=True)
        await ctx.send(embed=emb)
    
    else:
        await ctx.send("❌ '골드' 또는 '코인'을 입력하세요!")

# ── 👑 나만 쓸 수 있는 관리자 명령어: 지급 ──
# 여기에 본인 유저 ID를 입력하세요!
ADMIN_ID = 1456231020482265101 # 🔴 본인 디스코드 ID로 변경 필요

@bot.command(name='지급', description='[관리자만] 사용자에게 골드/코인을 지급합니다')
async def give(ctx, type_: str = None, member: discord.Member = None, amount: int = None):
    """
    사용법:
    `!지급 골드 @사용자 10000` - 사용자에게 골드 10000원 지급
    `!지급 코인 @사용자 50` - 사용자에게 코인 50개 지급
    """
    # 관리자 권한 체크 (본인 ID만)
    if ctx.author.id != ADMIN_ID:
        emb = discord.Embed(
            title="❌ 권한 없음",
            description="이 명령어는 관리자만 사용할 수 있습니다!",
            color=discord.Color.red()
        )
        return await ctx.send(embed=emb, delete_after=5)
    
    if not type_ or not member or not amount:
        return await ctx.send("사용법: `!지급 골드 @사용자 10000` 또는 `!지급 코인 @사용자 50`")
    
    if amount < 1:
        return await ctx.send("❌ 1개 이상의 양을 지급해야 합니다!")
    
    type_ = type_.lower()
    target_user = _u(member.id)
    admin_user = _u(ctx.author.id)
    
    if type_ == "골드":
        target_user["money"] += amount
        _esave(ECON)
        
        emb = discord.Embed(
            title="💰 골드 지급 완료!",
            description=f"{member.mention}님에게 **{_fmt(amount)}원** 지급됨!",
            color=discord.Color.gold()
        )
        emb.add_field(name="수령자 잔고", value=f"{_fmt(target_user['money'])}원", inline=False)
        emb.add_field(name="지급자", value=f"{ctx.author.mention}", inline=False)
        await ctx.send(embed=emb)
        
        # DM으로도 알림
        try:
            dm_emb = discord.Embed(
                title="💰 골드 수령!",
                description=f"**{_fmt(amount)}원**의 골드를 받았습니다!",
                color=discord.Color.gold()
            )
            dm_emb.add_field(name="현재 잔고", value=f"{_fmt(target_user['money'])}원", inline=False)
            dm_emb.set_footer(text="만물상 RPG 관리자로부터")
            await member.send(embed=dm_emb)
        except:
            pass  # DM 전송 실패해도 무시
    
    elif type_ == "코인":
        target_user["coins"] += amount
        _esave(ECON)
        
        emb = discord.Embed(
            title="💎 코인 지급 완료!",
            description=f"{member.mention}님에게 **💎 {amount}개** 지급됨!",
            color=discord.Color.purple()
        )
        emb.add_field(name="수령자 잔고", value=f"💎 {target_user['coins']}개", inline=False)
        emb.add_field(name="지급자", value=f"{ctx.author.mention}", inline=False)
        await ctx.send(embed=emb)
        
        # DM으로도 알림
        try:
            dm_emb = discord.Embed(
                title="💎 코인 수령!",
                description=f"**💎 {amount}개**의 코인을 받았습니다!",
                color=discord.Color.purple()
            )
            dm_emb.add_field(name="현재 잔고", value=f"💎 {target_user['coins']}개", inline=False)
            dm_emb.set_footer(text="만물상 RPG 관리자로부터")
            await member.send(embed=dm_emb)
        except:
            pass  # DM 전송 실패해도 무시
    
    else:
        await ctx.send("❌ '골드' 또는 '코인'을 입력하세요!")


# ============ 봇 실행 ============

if __name__ == "__main__":
    # 토큰 설정
    TOKEN = config.get("TOKEN", "YOUR_BOT_TOKEN_HERE")
    
    if TOKEN == "YOUR_BOT_TOKEN_HERE":
        print("❌ 토큰을 설정해주세요!")
        print(f"📝 bot_config.json 파일의 TOKEN을 수정하거나, 아래를 실행하세요:")
        print(f"python -c \"import json; c=json.load(open('bot_config.json')); c['TOKEN']='YOUR_TOKEN'; json.dump(c, open('bot_config.json','w'))\"")
    else:
        print("🚀 봇을 시작합니다...")
        bot.run(TOKEN)
