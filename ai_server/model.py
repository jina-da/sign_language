"""
Transformer Encoder 기반 수화 단어 분류 모델
- 입력: MediaPipe Keypoint 시퀀스 (T, 134)
  - pose 25개 + hand_left 21개 + hand_right 21개 = 67개 관절 × 2(x,y) = 134
- 출력: 1,000단어 분류 확률
- 가변 길이 시퀀스: Attention Mask로 처리
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

        # 위치 인코딩 행렬 생성 (max_len, d_model)
        pe = torch.zeros(max_len, d_model)
        position = torch.arange(0, max_len).unsqueeze(1).float()  # (max_len, 1)
        div_term = torch.exp(
            torch.arange(0, d_model, 2).float() * (-math.log(10000.0) / d_model)
        )

        pe[:, 0::2] = torch.sin(position * div_term)  # 짝수 인덱스: sin
        pe[:, 1::2] = torch.cos(position * div_term)  # 홀수 인덱스: cos
        pe = pe.unsqueeze(0)  # (1, max_len, d_model) - 배치 차원 추가

        # 학습 파라미터 아님 (고정값), 하지만 state_dict에는 포함
        self.register_buffer('pe', pe)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """
        x: (batch, T, d_model)
        반환: (batch, T, d_model) - 위치 정보가 더해진 텐서
        """
        x = x + self.pe[:, :x.size(1)]
        return self.dropout(x)


class SignLanguageTransformer(nn.Module):
    """
    수화 단어 분류 Transformer Encoder 모델

    구조:
    1. Input Projection: 134 → d_model(256)
    2. Positional Encoding: 프레임 순서 정보 추가
    3. Transformer Encoder: 6개 레이어, 8개 attention head
    4. Classifier: d_model(256) → num_classes(1000)
    """

    def __init__(
        self,
        input_dim: int = 134,       # keypoint 차원 (67관절 × 2)
        d_model: int = 256,         # Transformer 내부 차원
        num_heads: int = 8,         # Multi-head Attention 헤드 수
        num_layers: int = 6,        # Encoder 레이어 수
        num_classes: int = 1000,    # 분류할 단어 수
        dropout: float = 0.1,       # 드롭아웃 비율
        dim_feedforward: int = 1024 # FFN 내부 차원 (보통 d_model × 4)
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
            batch_first=True  # (batch, T, d_model) 형태 사용
        )
        self.transformer_encoder = nn.TransformerEncoder(
            encoder_layer,
            num_layers=num_layers
        )

        # 4. 분류기: 평균 풀링 후 1,000 클래스로 분류
        self.classifier = nn.Sequential(
            nn.Linear(d_model, d_model // 2),  # 256 → 128
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(d_model // 2, num_classes)  # 128 → 1000
        )

    def forward(
        self,
        x: torch.Tensor,
        padding_mask: torch.Tensor | None = None
    ) -> torch.Tensor:
        """
        x: (batch, T, 134) - keypoint 시퀀스
        padding_mask: (batch, T) - True인 위치는 패딩 (무시)
        반환: (batch, num_classes) - 각 단어 분류 로짓
        """

        # 1. 입력 차원 변환: (batch, T, 134) → (batch, T, 256)
        x = self.input_projection(x)

        # 2. 위치 정보 추가
        x = self.positional_encoding(x)

        # 3. Transformer Encoder 통과
        # padding_mask: 패딩된 프레임은 attention에서 무시
        x = self.transformer_encoder(x, src_key_padding_mask=padding_mask)

        # 4. 시퀀스 평균 풀링: (batch, T, 256) → (batch, 256)
        # 패딩 위치 제외하고 평균
        if padding_mask is not None:
            # 패딩 아닌 위치만 평균
            mask = (~padding_mask).unsqueeze(-1).float()  # (batch, T, 1)
            x = (x * mask).sum(dim=1) / mask.sum(dim=1)
        else:
            x = x.mean(dim=1)

        # 5. 분류: (batch, 256) → (batch, 1000)
        return self.classifier(x)


def build_model(num_classes: int = 1000) -> SignLanguageTransformer:
    """모델 생성 함수 - 개발계획서 스펙 기준"""
    return SignLanguageTransformer(
        input_dim=134,
        d_model=256,
        num_heads=8,
        num_layers=6,
        num_classes=num_classes,
        dropout=0.1,
        dim_feedforward=1024
    )


if __name__ == "__main__":
    # 모델 테스트
    model = build_model()

    # 파라미터 수 확인
    total_params = sum(p.numel() for p in model.parameters())
    print(f"총 파라미터 수: {total_params:,} ({total_params/1e6:.1f}M)")

    # 더미 입력으로 forward 테스트
    batch_size = 4
    seq_len = 50
    x = torch.randn(batch_size, seq_len, 134)
    out = model(x)
    print(f"입력 shape: {x.shape}")
    print(f"출력 shape: {out.shape}")  # (4, 1000) 나와야 함