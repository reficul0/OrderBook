// Советы по началу работы 
//   1. В окне обозревателя решений можно добавлять файлы и управлять ими.
//   2. В окне Team Explorer можно подключиться к системе управления версиями.
//   3. В окне "Выходные данные" можно просматривать выходные данные сборки и другие сообщения.
//   4. В окне "Список ошибок" можно просматривать ошибки.
//   5. Последовательно выберите пункты меню "Проект" > "Добавить новый элемент", чтобы создать файлы кода, или "Проект" > "Добавить существующий элемент", чтобы добавить в проект существующие файлы кода.
//   6. Чтобы снова открыть этот проект позже, выберите пункты меню "Файл" > "Открыть" > "Проект" и выберите SLN-файл.

#ifndef PCH_H
#define PCH_H

#define NOMINMAX
#ifdef _WIN32
	// Исключаем редко используемые компоненты из заголовков Windows
	#define WIN32_LEAN_AND_MEAN     
	#include <windows.h>  
#endif // _WIN32

#include <string>
#include <iostream>
#include <unordered_map>
#include <algorithm>

#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdio.h>

#include <math.h>
#include <vector>
#include <algorithm>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/optional/optional.hpp>

#include <boost/thread/synchronized_value.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#endif //PCH_H
