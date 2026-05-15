import asyncio
import json
import time
import random

# 설정 (ARCHITECTURE.pdf 기준)
SERVER_HOST = '127.0.0.1'
SERVER_PORT = 9000
CONCURRENT_USERS = 100  # 상정한 동시 접속자 수

async def simulate_client(user_id):
    """개별 클라이언트 시뮬레이션"""
    try:
        # 1. 서버 연결
        reader, writer = await asyncio.open_connection(SERVER_HOST, SERVER_PORT)

        # 2. 가상의 수화 데이터 생성 (Keypoints)
        dummy_data = {
            "type": "inference",
            "user_id": f"user_{user_id}",
            "word": "hello",
            "keypoints": [[random.random() for _ in range(75)] for _ in range(30)]  # 30프레임 데이터
        }
        payload = json.dumps(dummy_data).encode('utf-8')
        header = len(payload).to_bytes(4, byteorder='big')  # 4byte 헤더

        start_time = time.perf_counter()

        # 3. 데이터 전송
        writer.write(header + payload)
        await writer.drain()

        # 4. 서버 응답 대기
        resp_header = await reader.readexactly(4)
        resp_len = int.from_bytes(resp_header, byteorder='big')
        resp_payload = await reader.readexactly(resp_len)

        end_time = time.perf_counter()

        response = json.loads(resp_payload.decode('utf-8'))
        writer.close()
        await writer.wait_closed()

        return {
            "user_id": user_id,
            "latency": end_time - start_time,
            "status": response.get("status"),
            "result": response.get("prediction")
        }

    except Exception as e:
        return {"user_id": user_id, "error": str(e)}


async def run_load_test():
    print(f"🚀 {CONCURRENT_USERS}명의 동시 접속 테스트를 시작합니다...")

    # 50개의 태스크를 동시에 생성
    tasks = [simulate_client(i) for i in range(CONCURRENT_USERS)]

    start_test = time.perf_counter()
    results = await asyncio.gather(*tasks)
    end_test = time.perf_counter()

    # 결과 분석
    latencies = [r['latency'] for r in results if 'latency' in r]
    errors = [r for r in results if 'error' in r or r.get('status') == 'error']
    exhausted = [r for r in results if r.get('status') == 'AI_POOL_EXHAUSTED']

    print("\n" + "=" * 50)
    print(f"📊 테스트 결과 요약")
    print(f"- 총 테스트 시간: {end_test - start_test:.2f}초")
    print(f"- 성공: {len(latencies)} 명")
    print(f"- AI 자원 부족(Exhausted): {len(exhausted)} 명")
    print(f"- 기타 에러: {len(errors) - len(exhausted)} 명")

    if latencies:
        print(f"- 평균 응답 시간: {sum(latencies) / len(latencies):.4f}초")
        print(f"- 최소 응답 시간: {min(latencies):.4f}초")
        print(f"- 최대 응답 시간: {max(latencies):.4f}초")
    print("=" * 50)


if __name__ == "__main__":
    asyncio.run(run_load_test())