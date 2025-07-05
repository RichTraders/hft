#include <iostream>

int main() {
  constexpr auto kLang = "C++";
  std::cout << "Hello and welcome to " << kLang << "!\n";
  constexpr int kAmy = 5;
  for (int i = 1; i <= kAmy; i++) {
    std::cout << "i = " << i << '\n';
  }

  return 0;
}

// TIP See CLion help at <a
// href="https://www.jetbrains.com/help/clion/">jetbrains.com/help/clion/</a>.
//  Also, you can try interactive lessons for CLion by selecting
//  'Help | Learn IDE Features' from the main menu.