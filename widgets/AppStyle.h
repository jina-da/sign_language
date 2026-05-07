#pragma once
#include <QString>

/**
 * AppStyle — 앱 전체에서 쓰는 색상·스타일 상수 모음
 *
 * 디자인을 수정하고 싶으면 이 파일의 값만 바꾸면
 * 모든 위젯에 자동으로 반영됩니다.
 */
namespace AppStyle
{
    // ── 색상 ─────────────────────────────────────────
    constexpr auto C_GREEN_DARK   = "#3B6D11";   // 버튼·헤더·강조
    constexpr auto C_GREEN_MID    = "#639922";   // 서브 텍스트·아이콘
    constexpr auto C_GREEN_LIGHT  = "#97C459";   // 연한 강조
    constexpr auto C_GREEN_BG     = "#EAF3DE";   // 카드 배경·배지
    constexpr auto C_GREEN_BORDER = "#C0DD97";   // 테두리
    constexpr auto C_PAGE_BG      = "#F0F7EE";   // 전체 페이지 배경
    constexpr auto C_WHITE        = "#FFFFFF";
    constexpr auto C_TEXT_DARK    = "#27500A";   // 주요 텍스트
    constexpr auto C_TEXT_MID     = "#5F5E5A";   // 보조 텍스트
    constexpr auto C_TEXT_MUTED   = "#888780";   // 흐린 텍스트
    constexpr auto C_TEXT_HINT    = "#B4B2A9";   // 플레이스홀더
    constexpr auto C_AMBER_BG     = "#FFF8E6";   // 복습 배지 배경
    constexpr auto C_AMBER_TEXT   = "#854F0B";   // 복습 배지 텍스트
    constexpr auto C_PINK_BG      = "#FBEAF0";   // 게임 배지 배경
    constexpr auto C_PINK_TEXT    = "#72243E";   // 게임 배지 텍스트
    constexpr auto C_BLUE_BG      = "#E6F1FB";   // 사전 배지 배경
    constexpr auto C_BLUE_TEXT    = "#0C447C";   // 사전 배지 텍스트

    // ── 앱 이름 (나중에 한 곳만 수정) ───────────────
    constexpr auto APP_NAME       = "수화배움";
    constexpr auto APP_SUBTITLE   = "한국 수화 학습 시스템";

    // ── 공통 QSS 조각 ────────────────────────────────

    // 초록 주요 버튼
    inline QString btnPrimary() {
        return QString(
            "QPushButton {"
            "  background: %1; color: white;"
            "  border: none; border-radius: 10px;"
            "  font-size: 15px; font-weight: 500; padding: 10px;"
            "}"
            "QPushButton:hover  { background: %2; }"
            "QPushButton:pressed{ background: %2; }"
        ).arg(C_GREEN_DARK, C_TEXT_DARK);
    }

    // 외곽선 보조 버튼
    inline QString btnOutline() {
        return QString(
            "QPushButton {"
            "  background: transparent; color: %1;"
            "  border: 0.5px solid %2; border-radius: 10px;"
            "  font-size: 14px; padding: 9px;"
            "}"
            "QPushButton:hover { background: %3; }"
        ).arg(C_GREEN_DARK, C_GREEN_LIGHT, C_GREEN_BG);
    }

    // 입력 필드
    inline QString lineEdit() {
        return QString(
            "QLineEdit {"
            "  border: 0.5px solid %1; border-radius: 10px;"
            "  padding: 0 12px 0 38px;"
            "  font-size: 14px; color: %2;"
            "  background: #FAFFF8; height: 40px;"
            "}"
            "QLineEdit:focus {"
            "  border-color: %3; background: #F3FBED;"
            "}"
            "QLineEdit[echoMode=\"2\"] { padding-right: 36px; }"
        ).arg(C_GREEN_BORDER, C_TEXT_DARK, C_GREEN_MID);
    }

    // 카드 (흰 배경)
    inline QString card() {
        return QString(
            "background: %1;"
            "border-radius: 14px;"
            "border: 0.5px solid %2;"
        ).arg(C_WHITE, C_GREEN_BORDER);
    }

    // 페이지 배경
    inline QString pageBg() {
        return QString("background: %1;").arg(C_PAGE_BG);
    }
}
