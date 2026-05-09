"""
수화 단어 분류 모델
- Transformer Encoder 기반 모델
- GRU 기반 모델 (데이터 적을 때 유리)
- 입력: MediaPipe Keypoint 시퀀스 (T, 134)
  - pose 25개 + hand_left 21개 + hand_right 21개 = 67개 관절 × 2(x,y) = 134
- 출력: 1,000단어 분류 확률
"""

import torch
import torch.nn as nn
import math


class PositionalEncoding(nn.Module):
    """
    프레임 순서 정보를 임베딩에 추가하는 모듈
    Transformer는 순서 개념이 없어서 별도로 위치 정보를 주입해야 함
    sin/cos 함수로 각 위치마다 고유한 패턴 생성
    """

    def __init__(self, d_model: int, max_len: int = 512, dropout: float = 0.1):
        super().__init__()
        self.dropout = nn.Dropout(p=dropout)

        pe = torch.zeros(max_len, d_model)
        position = torch.arange(0, max_len).unsqueeze(1).float()
        div_term = torch.exp(
            torch.arange(0, d_model, 2).float() * (-math.log(10000.0) / d_model)
        )

        pe[:, 0::2] = torch.sin(position * div_term)
        pe[:, 1::2] = torch.cos(position * div_term)
        pe = pe.unsqueeze(0)
        self.register_buffer('pe', pe)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = x + self.pe[:, :x.size(1)]
        return self.dropout(x)


class SignLanguageTransformer(nn.Module):
    """
    수화 단어 분류 Transformer Encoder 모델
    구조: Input Projection → Positional Encoding → Transformer Encoder → Classifier
    """

    def __init__(
        self,
        input_dim: int = 134,
        d_model: int = 256,
        num_heads: int = 8,
        num_layers: int = 6,
        num_classes: int = 1000,
        dropout: float = 0.1,
        dim_feedforward: int = 1024
    ):
        super().__init__()

        # 1. 입력 차원 변환: 134 → 256
        self.input_projection = nn.Linear(input_dim, d_model)

        # 2. 위치 인코딩
        self.positional_encoding = PositionalEncoding(d_model, dropout=dropout)

        # 3. Transformer Encoder
        encoder_layer = nn.TransformerEncoderLayer(
            d_model=d_model,
            nhead=num_heads,
            dim_feedforward=dim_feedforward,
            dropout=dropout,
            batch_first=True
        )
        self.transformer_encoder = nn.TransformerEncoder(
            encoder_layer,
            num_layers=num_layers
        )

        # 4. 분류기
        self.classifier = nn.Sequential(
            nn.Linear(d_model, d_model // 2),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(d_model // 2, num_classes)
        )

    def forward(
        self,
        x: torch.Tensor,
        padding_mask: torch.Tensor | None = None
    ) -> torch.Tensor:
        x = self.input_projection(x)
        x = self.positional_encoding(x)
        x = self.transformer_encoder(x, src_key_padding_mask=padding_mask)

        if padding_mask is not None:
            mask = (~padding_mask).unsqueeze(-1).float()
            x = (x * mask).sum(dim=1) / mask.sum(dim=1)
        else:
            x = x.mean(dim=1)

        return self.classifier(x)


class SignLanguageGRU(nn.Module):
    """
    GRU 기반 수화 단어 분류 모델
    - Transformer 대비 데이터 적을 때 유리
    - 양방향 GRU로 시퀀스 앞뒤 맥락 모두 학습
    구조: Bidirectional GRU → 평균 풀링 → Classifier
    """

    def __init__(
        self,
        input_dim: int = 134,      # keypoint 차원
        hidden_dim: int = 256,     # GRU hidden 차원
        num_layers: int = 3,       # GRU 레이어 수
        num_classes: int = 1000,   # 분류할 단어 수
        dropout: float = 0.3       # 드롭아웃 비율
    ):
        super().__init__()

        # 양방향 GRU (앞→뒤 + 뒤→앞 동시 학습)
        self.gru = nn.GRU(
            input_size=input_dim,
            hidden_size=hidden_dim,
            num_layers=num_layers,
            batch_first=True,
            dropout=dropout,
            bidirectional=True  # 양방향이라 출력이 hidden_dim * 2
        )

        # 분류기 (양방향이라 hidden_dim * 2 입력)
        self.classifier = nn.Sequential(
            nn.Linear(hidden_dim * 2, hidden_dim),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden_dim, num_classes)
        )

    def forward(
        self,
        x: torch.Tensor,
        padding_mask: torch.Tensor | None = None
    ) -> torch.Tensor:
        """
        x: (batch, T, 134)
        반환: (batch, num_classes)
        """
        # GRU 통과 → (batch, T, hidden_dim*2)
        out, _ = self.gru(x)

        # 패딩 제외 평균 풀링
        if padding_mask is not None:
            mask = (~padding_mask).unsqueeze(-1).float()
            out = (out * mask).sum(dim=1) / mask.sum(dim=1)
        else:
            out = out.mean(dim=1)

        return self.classifier(out)


def build_model(num_classes: int = 1000) -> SignLanguageTransformer:
    """Transformer 모델 생성 - 개발계획서 스펙 기준"""
    return SignLanguageTransformer(
        input_dim=134,
        d_model=256,
        num_heads=8,
        num_layers=6,
        num_classes=num_classes,
        dropout=0.1,
        dim_feedforward=1024
    )


def build_gru_model(num_classes: int = 1000) -> SignLanguageGRU:
    """GRU 모델 생성"""
    return SignLanguageGRU(
        input_dim=134,
        hidden_dim=256,
        num_layers=3,
        num_classes=num_classes,
        dropout=0.3
    )


if __name__ == "__main__":
    # Transformer 테스트
    transformer = build_model()
    total_params = sum(p.numel() for p in transformer.parameters())
    print(f"Transformer 파라미터 수: {total_params:,} ({total_params/1e6:.1f}M)")

    # GRU 테스트
    gru = build_gru_model()
    total_params = sum(p.numel() for p in gru.parameters())
    print(f"GRU 파라미터 수: {total_params:,} ({total_params/1e6:.1f}M)")

    # 더미 입력 테스트
    x = torch.randn(4, 50, 134)
    print(f"Transformer 출력: {transformer(x).shape}")
    print(f"GRU 출력: {gru(x).shape}")