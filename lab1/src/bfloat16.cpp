#include "bfloat16.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
constexpr int MAX_FRACTION_BITS = BFloat16Converter::BIAS
                                + BFloat16Converter::MANTISSA_BITS + 2;
}

BFloat16 BFloat16Converter::encode(double value) {
    const bool sign = std::signbit(value);

    // по спеке:
    // - inf: 11111111, 0000000
    // - nan: 11111111, 1000000 <- ну вообще любая
    if (std::isnan(value)) {
        return {sign, 0xFF, 0x40};
    }
    if (std::isinf(value)) {
        return {sign, 0xFF, 0};
    }
    if (value == 0.0) {
        return {sign, 0, 0};
    }

    const double module = std::abs(value);
    int exponent = 0;
    std::string mantissaSource;

    if (module <= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        const auto integerPart = static_cast<std::uint64_t>(module);
        const double fractionalPart = module - static_cast<double>(integerPart);

        const std::string integerBits = unsignedIntegerToBinary(integerPart);
        const std::string fractionalBits = fractionalPartToBinary(fractionalPart);
        normalize(integerBits, fractionalBits, exponent, mantissaSource);
    } else {
        // для чисел, не помещающихся в uint64_t, нормализуем значение
        // последовательным делением на 2
        double normalized = module;
        while (normalized >= 2) {
            normalized /= 2;
            ++exponent;
        }
        mantissaSource = fractionalPartToBinary(normalized - 1);
    }

    bool exponentCarry = false;
    const std::uint8_t mantissa = roundMantissa(mantissaSource, exponentCarry);
    const int biasedExponent = exponent + (exponentCarry ? 1 : 0) + BIAS;

    if (biasedExponent >= 0xFF) {
        return {sign, 0xFF, 0};
    }

    // слишком малые значения округляются к нулю.
    if (biasedExponent <= 0) {
        return {sign, 0, 0};
    }

    return {sign, static_cast<std::uint8_t>(biasedExponent), mantissa};
}

double BFloat16Converter::decode(const BFloat16& value) {
    const double sign = value.sign ? -1 : 1;
    const double fraction = mantissaToFraction(value.mantissa);

    if (value.exponent == 0) {
        if (value.mantissa == 0) {
            return std::copysign(0.0, sign);
        }
        return sign * fraction * std::pow(2.0, 1 - BIAS);
    }

    if (value.exponent == 0xFF) {
        if (value.mantissa == 0) {
            return std::copysign(std::numeric_limits<double>::infinity(), sign);
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    return sign * (1 + fraction)
         * std::pow(2, static_cast<int>(value.exponent) - BIAS);
}

BFloat16 BFloat16Converter::fromBitString(std::string_view bits) {
    if (bits.size() != TOTAL_BITS) {
        throw std::invalid_argument("ожидается ровно 16 бит");
    }

    for (const char bit : bits) {
        if (bit != '0' && bit != '1') {
            throw std::invalid_argument("строка должна состоять только из 0 и 1");
        }
    }

    BFloat16 result{};
    result.sign = bits.front() == '1';

    for (int i = 1; i <= EXPONENT_BITS; ++i) {
        result.exponent = static_cast<std::uint8_t>(
            result.exponent * 2 + (bits[i] - '0'));
    }
    for (int i = 1 + EXPONENT_BITS; i < TOTAL_BITS; ++i) {
        result.mantissa = static_cast<std::uint8_t>(
            result.mantissa * 2 + (bits[i] - '0'));
    }

    return result;
}

std::string BFloat16Converter::toBitString(const BFloat16& value) {
    if (value.mantissa > 0x7F) {
        throw std::invalid_argument("мантисса bf16 не может занимать больше 7 бит");
    }

    std::string result;
    result.reserve(TOTAL_BITS);
    result.push_back(value.sign ? '1' : '0');

    // переводим в строки сдвигом, читая по одному последнему биту
    for (int bit = EXPONENT_BITS - 1; bit >= 0; --bit) {
        result.push_back(((value.exponent >> bit) & 1) ? '1' : '0');
    }
    for (int bit = MANTISSA_BITS - 1; bit >= 0; --bit) {
        result.push_back(((value.mantissa >> bit) & 1) ? '1' : '0');
    }

    return result;
}

std::string BFloat16Converter::describe(const BFloat16& value) {
    const std::string bits = toBitString(value);
    std::ostringstream output;
    output << "Знак:       " << bits[0]
           << (value.sign ? " (отрицательный)" : " (положительный)") << '\n'
           << "Экспонента: " << bits.substr(1, EXPONENT_BITS)
           << " (смещённая: " << static_cast<int>(value.exponent);

    if (value.exponent > 0 && value.exponent < 0xFF) {
        output << ", истинная: " << static_cast<int>(value.exponent) - BIAS;
    }

    output << ")\nМантисса:   " << bits.substr(1 + EXPONENT_BITS)
           << " (дробная часть: " << mantissaToFraction(value.mantissa) << ')';
    return output.str();
}

// перевод числовой части в двоичную сис-му
std::string BFloat16Converter::unsignedIntegerToBinary(std::uint64_t value) {
    if (value == 0) {
        return "0";
    }

    std::string result;
    while (value > 0) {
        result.insert(result.begin(), value % 2 == 0 ? '0' : '1');
        value /= 2;
    }
    return result;
}

// перевод дробной части в двоичную сис-му
std::string BFloat16Converter::fractionalPartToBinary(double value) {
    std::string result;
    result.reserve(MAX_FRACTION_BITS);

    for (int i = 0; i < MAX_FRACTION_BITS && value != 0; ++i) {
        value *= 2;
        const int bit = static_cast<int>(value);
        result.push_back(bit == 0 ? '0' : '1');
        value -= bit;
    }
    return result;
}

void BFloat16Converter::normalize(const std::string& integerBits,
                                  const std::string& fractionalBits,
                                  int& exponent,
                                  std::string& mantissaSource) {
    if (integerBits != "0") {
        exponent = static_cast<int>(integerBits.size()) - 1;
        mantissaSource = integerBits.substr(1) + fractionalBits;
        return;
    }

    const auto firstOne = fractionalBits.find('1');
    // если не нашли ни одной единицы в дробной части
    if (firstOne == std::string::npos) {
        exponent = -BIAS;
        mantissaSource.clear();
        return;
    }

    exponent = -static_cast<int>(firstOne) - 1;
    mantissaSource = fractionalBits.substr(firstOne + 1);
}

std::uint8_t BFloat16Converter::roundMantissa(const std::string& bits,
                                              bool& exponentCarry) {
    std::string padded = bits;
    padded.resize(MANTISSA_BITS + 1, '0');

    unsigned value = 0;
    for (int i = 0; i < MANTISSA_BITS; ++i) {
        value = value * 2 + (padded[i] - '0');
    }

    if (padded[MANTISSA_BITS] == '1') {
        ++value;
    }

    exponentCarry = value > 0x7F;
    return exponentCarry ? 0 : static_cast<std::uint8_t>(value);
}

double BFloat16Converter::mantissaToFraction(std::uint8_t mantissa) {
    double result = 0;
    double weight = 0.5;

    for (int bit = MANTISSA_BITS - 1; bit >= 0; --bit) {
        if ((mantissa >> bit) & 1) {
            result += weight;
        }
        weight /= 2.0;
    }
    return result;
}
