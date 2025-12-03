main()
{
    int x, y, sum1, sum2, sumComm, sum3, prod, guard;
    x = 2;
    y = 3;

    sum1 = x + y;
    sumComm = y + x;
    sum2 = x + y;
    guard = sum1 + sumComm + sum2;

    x = x + 1;
    sum3 = x + y;

    prod = sum2 * sum3;

    output prod;
    output guard;
}