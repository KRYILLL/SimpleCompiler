main()
{
    int a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z;
    int total, result, verify;
    int loop1, loop2, loop3;
    int expr1, expr2, expr3, expr4, expr5, expr6;
    int temp1, temp2, temp3, temp4;
    int nested1, nested2, nested3, nested4;
    int deep1, deep2, deep3, deep4;
    int strength1, strength2, strength3, strength4;
    int dead1, dead2, dead3, dead4;
    int midexpr, midexprcopy;
    int condvar;
    int branch1, branch2, branch3, branch4;
    int outerexpr, outerexprdup;
    int complexchain1, complexchain2, complexchain3, complexchain4, complexchain5, complexchain6;
    int alwaystrue, innerif, deeper, deepest;
    int zero, multzero, addzero, subzero;
    int finalcalc1, finalcalc2, finalcalc3, finalcalc4, finalcalc5, finalcalc6;
    int postdead1, postdead2, postdead3;
    int counter, inner, deep;
    int cnt1, cnt2, cnt3;
    int outerloop, midloop, innerloop;
    int calc1, calc2, calc3, calc4, calc5, calc6;
    int val1, val2, val3, val4, val5, val6;
    int sum1, sum2, sum3, sum4, sum5, sum6;
    int test1, test2, test3, test4, test5, test6;
    
    a = 1;
    b = 2;
    c = 3;
    d = 4;
    e = 5;
    f = 6;
    g = 7;
    h = 8;
    i = 9;
    j = 10;
    k = 11;
    l = 12;
    m = 13;
    n = 14;
    o = 15;
    p = 16;
    q = 17;
    r = 18;
    s = 19;
    t = 20;
    u = 21;
    v = 22;
    w = 23;
    x = 24;
    y = 25;
    z = 26;
    
    total = 0;
    loop1 = 100;
    
    while (loop1 > 0)
    {
        loop2 = 50;
        
        while (loop2 > 0)
        {
            loop3 = 20;
            
            while (loop3 > 0)
            {
                expr1 = a * b + c * d - e / f + g / h;
                expr2 = a * b + c * d - e / f + g / h;
                expr3 = i * j - k * l + m / n - o / p;
                expr4 = i * j - k * l + m / n - o / p;
                
                temp1 = expr1 + expr3;
                temp2 = expr2 + expr4;
                temp3 = expr1 - expr4;
                temp4 = expr2 - expr3;
                
                nested1 = (temp1 + temp2) * (temp3 - temp4);
                nested2 = (temp1 + temp2) * (temp3 - temp4);
                nested3 = (temp1 * temp2) + (temp3 * temp4);
                nested4 = (temp1 * temp2) + (temp3 * temp4);
                
                deep1 = (nested1 + nested3) / (nested2 - nested4);
                deep2 = (nested1 + nested3) / (nested2 - nested4);
                deep3 = (nested1 * nested3) - (nested2 * nested4);
                deep4 = (nested1 * nested3) - (nested2 * nested4);
                
                total = total + deep1 + deep2 + deep3 + deep4;
                
                strength1 = loop3 * 7;
                strength2 = loop3 * 7 + 3;
                strength3 = loop3 * 7 - 5;
                strength4 = loop3 * 7 * 2;
                
                total = total + strength1 + strength2 + strength3 + strength4;
                
                dead1 = strength1 * 2;
                dead2 = dead1 + 5;
                dead3 = dead2 * 3;
                dead4 = dead3 / 2;
                
                loop3 = loop3 - 1;
            }
            
            midexpr = q * r + s * t - u / v + w / x;
            midexprcopy = q * r + s * t - u / v + w / x;
            
            condvar = loop2 * 2;
            
            if (condvar > 75)
            {
                branch1 = midexpr + 100;
                branch2 = midexprcopy + 100;
                total = total + branch1 + branch2;
            }
            else
            {
                branch3 = midexpr - 50;
                branch4 = midexprcopy - 50;
                total = total + branch3 + branch4;
            }
            
            if (loop2 > 1000)
            {
                branch1 = 999999;
                total = total + branch1;
            }
            
            loop2 = loop2 - 1;
        }
        
        outerexpr = y * z + a * b - c / d + e / f;
        outerexprdup = y * z + a * b - c / d + e / f;
        
        complexchain1 = outerexpr * 2;
        complexchain2 = complexchain1 + outerexprdup;
        complexchain3 = complexchain2 * 3;
        complexchain4 = complexchain3 - outerexpr;
        complexchain5 = complexchain4 / 4;
        complexchain6 = complexchain5 + outerexprdup;
        
        total = total + complexchain1 + complexchain2 + complexchain3;
        total = total + complexchain4 + complexchain5 + complexchain6;
        
        alwaystrue = loop1 + 1;
        
        if (alwaystrue > 0)
        {
            innerif = total * 2;
            
            if (innerif > 0)
            {
                deeper = innerif + 5;
                
                if (deeper > 10)
                {
                    deepest = deeper * 3;
                    total = total + deepest;
                }
            }
        }
        
        zero = 0;
        multzero = total * zero;
        addzero = multzero + zero;
        subzero = addzero - zero;
        
        loop1 = loop1 - 1;
    }
    
    finalcalc1 = total * 2;
    finalcalc2 = total * 2;
    finalcalc3 = finalcalc1 + finalcalc2;
    finalcalc4 = finalcalc1 + finalcalc2;
    finalcalc5 = finalcalc3 * finalcalc4;
    finalcalc6 = finalcalc3 * finalcalc4;
    
    result = finalcalc5 + finalcalc6;
    
    postdead1 = result * 3;
    postdead2 = postdead1 / 2;
    postdead3 = postdead2 + 100;
    
    output result;
    
    verify = 0;
    counter = 100;
    
    while (counter > 0)
    {
        inner = 50;
        
        while (inner > 0)
        {
            deep = 20;
            
            while (deep > 0)
            {
                verify = verify + 1;
                deep = deep - 1;
            }
            
            inner = inner - 1;
        }
        
        counter = counter - 1;
    }
    
    output verify;
}