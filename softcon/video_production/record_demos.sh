#!/bin/bash
# Mini-KVM 데모 녹화 스크립트
# OBS Studio로 녹화할 터미널 명령어들을 자동화

set -e

# 색상 정의
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 작업 디렉토리 설정
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KVM_DIR="$SCRIPT_DIR/../kvm-vmm-x86"
OUTPUT_DIR="$SCRIPT_DIR/demo_recordings"

echo -e "${GREEN}=== Mini-KVM 데모 녹화 스크립트 ===${NC}"
echo ""

# 출력 디렉토리 생성
mkdir -p "$OUTPUT_DIR"

# kvm-vmm-x86 디렉토리로 이동
cd "$KVM_DIR"

# VMM 빌드 확인
if [ ! -f "kvm-vmm" ]; then
    echo -e "${YELLOW}VMM이 빌드되지 않았습니다. 빌드를 시작합니다...${NC}"
    make vmm
fi

# 게스트 빌드 확인
if [ ! -f "guest/hello.bin" ]; then
    echo -e "${YELLOW}게스트가 빌드되지 않았습니다. 빌드를 시작합니다...${NC}"
    make guests
fi

# 1K OS 빌드 확인
if [ ! -f "os-1k/kernel.bin" ]; then
    echo -e "${YELLOW}1K OS가 빌드되지 않았습니다. 빌드를 시작합니다...${NC}"
    make 1k-os
fi

echo -e "${GREEN}모든 바이너리가 준비되었습니다.${NC}"
echo ""

# 데모 함수들
demo_pause() {
    echo -e "${YELLOW}[녹화 준비] $1${NC}"
    echo -e "${YELLOW}준비되면 Enter를 누르세요...${NC}"
    read -r
}

demo_clear() {
    clear
    sleep 0.5
}

demo_type() {
    local text="$1"
    echo -e "${GREEN}$ ${NC}$text"
    sleep 1
}

# =============================================================================
# 데모 1: 4 vCPU 병렬 실행 (인트로 훅용)
# =============================================================================
demo_1_multi_vcpu_intro() {
    demo_clear
    demo_pause "데모 1: 4 vCPU 병렬 실행 (20초)"
    
    demo_type "./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin guest/minimal.bin"
    ./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin guest/minimal.bin
    
    sleep 2
}

# =============================================================================
# 데모 2: 단일 vCPU 빠른 실행
# =============================================================================
demo_2_single_vcpu() {
    demo_clear
    demo_pause "데모 2: 단일 vCPU 빠른 실행 (15초)"
    
    echo -e "${GREEN}=== Hello World ===${NC}"
    demo_type "./kvm-vmm guest/hello.bin"
    ./kvm-vmm guest/hello.bin
    sleep 1
    
    demo_clear
    echo -e "${GREEN}=== Minimal Guest (1 byte) ===${NC}"
    demo_type "./kvm-vmm guest/minimal.bin"
    time ./kvm-vmm guest/minimal.bin
    sleep 2
}

# =============================================================================
# 데모 3: 2 vCPU 병렬 실행
# =============================================================================
demo_3_dual_vcpu() {
    demo_clear
    demo_pause "데모 3: 2 vCPU 병렬 실행 (25초)"
    
    demo_type "./kvm-vmm guest/multiplication.bin guest/counter.bin"
    ./kvm-vmm guest/multiplication.bin guest/counter.bin
    
    sleep 2
}

# =============================================================================
# 데모 4: 4 vCPU 최대 병렬성
# =============================================================================
demo_4_quad_vcpu() {
    demo_clear
    demo_pause "데모 4: 4 vCPU 최대 병렬성 (20초)"
    
    demo_type "./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin guest/fibonacci.bin"
    ./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin guest/fibonacci.bin
    
    sleep 2
}

# =============================================================================
# 데모 5: 1K OS - 곱셈표 (프로그램 1)
# =============================================================================
demo_5_1k_os_multiplication() {
    demo_clear
    demo_pause "데모 5: 1K OS - 곱셈표 (20초)"
    
    demo_type "printf '1\\n0\\n' | ./kvm-vmm --paging os-1k/kernel.bin"
    printf '1\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin
    
    sleep 2
}

# =============================================================================
# 데모 6: 1K OS - 피보나치 (프로그램 4)
# =============================================================================
demo_6_1k_os_fibonacci() {
    demo_clear
    demo_pause "데모 6: 1K OS - 피보나치 (20초)"
    
    demo_type "printf '4\\n0\\n' | ./kvm-vmm --paging os-1k/kernel.bin"
    printf '4\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin
    
    sleep 2
}

# =============================================================================
# 데모 7: 1K OS - 에코 (프로그램 3) - 대화형
# =============================================================================
demo_7_1k_os_echo() {
    demo_clear
    demo_pause "데모 7: 1K OS - 에코 (대화형, 20초)"
    
    echo -e "${YELLOW}이 데모는 대화형입니다. 다음을 입력하세요:${NC}"
    echo -e "${YELLOW}1. 메뉴에서 '3' 입력 (Echo)${NC}"
    echo -e "${YELLOW}2. 'Mini-KVM Demo!' 입력${NC}"
    echo -e "${YELLOW}3. 'quit' 입력하여 종료${NC}"
    echo -e "${YELLOW}4. 메뉴에서 '0' 입력 (Exit)${NC}"
    echo ""
    
    demo_type "./kvm-vmm --paging os-1k/kernel.bin"
    ./kvm-vmm --paging os-1k/kernel.bin
    
    sleep 2
}

# =============================================================================
# 데모 8: 1K OS - 소수 (프로그램 5)
# =============================================================================
demo_8_1k_os_primes() {
    demo_clear
    demo_pause "데모 8: 1K OS - 소수 (15초)"
    
    demo_type "printf '5\\n0\\n' | ./kvm-vmm --paging os-1k/kernel.bin"
    printf '5\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin
    
    sleep 2
}

# =============================================================================
# 메인 메뉴
# =============================================================================
show_menu() {
    echo ""
    echo -e "${GREEN}=== 데모 선택 ===${NC}"
    echo "1) 데모 1: 4 vCPU 병렬 실행 (인트로 훅)"
    echo "2) 데모 2: 단일 vCPU 빠른 실행"
    echo "3) 데모 3: 2 vCPU 병렬 실행"
    echo "4) 데모 4: 4 vCPU 최대 병렬성"
    echo "5) 데모 5: 1K OS - 곱셈표"
    echo "6) 데모 6: 1K OS - 피보나치"
    echo "7) 데모 7: 1K OS - 에코 (대화형)"
    echo "8) 데모 8: 1K OS - 소수"
    echo "---"
    echo "a) 모든 데모 순차 실행"
    echo "q) 종료"
    echo ""
    echo -e "${YELLOW}선택: ${NC}"
}

# 메인 루프
while true; do
    show_menu
    read -r choice
    
    case $choice in
        1) demo_1_multi_vcpu_intro ;;
        2) demo_2_single_vcpu ;;
        3) demo_3_dual_vcpu ;;
        4) demo_4_quad_vcpu ;;
        5) demo_5_1k_os_multiplication ;;
        6) demo_6_1k_os_fibonacci ;;
        7) demo_7_1k_os_echo ;;
        8) demo_8_1k_os_primes ;;
        a)
            echo -e "${GREEN}모든 데모를 순차 실행합니다...${NC}"
            demo_1_multi_vcpu_intro
            demo_2_single_vcpu
            demo_3_dual_vcpu
            demo_4_quad_vcpu
            demo_5_1k_os_multiplication
            demo_6_1k_os_fibonacci
            demo_7_1k_os_echo
            demo_8_1k_os_primes
            echo -e "${GREEN}모든 데모가 완료되었습니다!${NC}"
            ;;
        q)
            echo -e "${GREEN}종료합니다.${NC}"
            exit 0
            ;;
        *)
            echo -e "${RED}잘못된 선택입니다.${NC}"
            ;;
    esac
done
