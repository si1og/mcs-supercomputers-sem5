#include "cli.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

void CLI::run() {
    while (true) {
        m_printMenu();

        switch (m_readInt("> ", 0, 2)) {
            case 0:
                m_printInfo("Завершение работы.");
                return;
            case 1:
                m_menuDecimalToBFloat16();
                break;
            case 2:
                m_menuBFloat16ToDecimal();
                break;
        }
    }
}

void CLI::m_printMenu() const {
    std::cout << "\n===== Конвертер bfloat16 =====\n"
              << "  1 — десятичное число -> bfloat16\n"
              << "  2 — bfloat16 -> десятичное число\n"
              << "  0 — выход\n";
}

void CLI::m_printHeader(const std::string& title) const {
    std::cout << "\n===== " << title << " =====\n";
}

void CLI::m_printInfo(const std::string& message) const {
    std::cout << "[*] " << message << '\n';
}

void CLI::m_printError(const std::string& message) const {
    std::cout << "[x] " << message << '\n';
}

int CLI::m_readInt(const std::string& prompt, int min, int max) const {
    while (true) {
        std::cout << prompt;
        int value{};

        if (std::cin >> value && value >= min && value <= max) {
            return value;
        }

        m_printError("Некорректный ввод.");
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

double CLI::m_readDouble(const std::string& prompt) const {
    while (true) {
        std::cout << prompt;
        double value{};

        if (std::cin >> value) {
            return value;
        }

        m_printError("Ожидается вещественное число.");
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

std::string CLI::m_readBitString(const std::string& prompt) const {
    while (true) {
        std::cout << prompt;
        std::string bits;
        std::cin >> bits;

        try {
            BFloat16Converter::fromBitString(bits);
            return bits;
        } catch (const std::invalid_argument& error) {
            m_printError(error.what());
        }
    }
}

void CLI::m_menuDecimalToBFloat16() const {
    m_printHeader("Десятичное число -> bfloat16");
    const double input = m_readDouble("Введите число: ");
    const BFloat16 result = BFloat16Converter::encode(input);

    m_printInfo("Число закодировано.");
    m_printNumber(result);
}

void CLI::m_menuBFloat16ToDecimal() const {
    m_printHeader("bfloat16 -> десятичное число");
    const std::string bits = m_readBitString("Введите 16 бит: ");
    const BFloat16 value = BFloat16Converter::fromBitString(bits);

    m_printNumber(value);
    std::cout << "Десятичное значение: " << std::setprecision(17)
              << BFloat16Converter::decode(value) << '\n';
}

void CLI::m_printNumber(const BFloat16& value) const {
    std::cout << "Битовое представление: "
              << BFloat16Converter::toBitString(value) << '\n'
              << BFloat16Converter::describe(value) << '\n';
}
