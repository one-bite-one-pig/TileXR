# 2026 年 7 月实习报告

主文件：`internship_report.tex`

报告面向不了解昇腾和通信算子的读者，内容主线为：

1. 当前 AI 计算面临的容量、带宽和协同问题；
2. 多芯片协同与超节点；
3. 昇腾架构、Ascend C、UB 和 UDMA/URMA；
4. TileXR EP Combine 算子的开发实践；
5. 代码修改、独立编译、正确性验证和性能分析闭环；
6. 本月总结、后续计划、术语表和实验复现清单。

## 编译

需要带有 `ctex` 和 TikZ 的 TeX Live，使用 XeLaTeX 编译：

```bash
latexmk -xelatex internship_report.tex
```

也可以连续运行两次：

```bash
xelatex internship_report.tex
xelatex internship_report.tex
```

提交前请填写标题页中的姓名、学校/专业、实习部门和指导老师。
