<p align="center">
  <img src="assets/logo.png" width="140" alt="Logo Brendler">
</p>

<h1 align="center">Watermark Remover by Brendler</h1>

<p align="center">
  Remove a marca d'água das versões <b>Insider Preview</b> do Windows 11<br>
  (<i>"Windows 11 Pro Insider Preview — Cópia de avaliação. Build …"</i>)<br>
  <b>sem alterar nenhum arquivo do sistema.</b>
</p>

<p align="center">
  <a href="https://github.com/fbrendler/WatermarkRemover-Brendler/raw/main/download/WatermarkRemoverByBrendler.zip"><b>⬇️ Baixar WatermarkRemoverByBrendler.zip (v2.0)</b></a>
</p>

---

## ✨ Recursos

- **Um clique:** a marca d'água some na hora, sem reiniciar o PC.
- **Seguro:** não modifica, substitui nem apaga arquivos do Windows.
- **Funciona depois das atualizações:** a cada nova build do Windows o programa se ajusta sozinho.
- **Remoção automática (opcional):** pode rodar escondido a cada login, para você não precisar fazer nada.
- **Leve:** um único `.exe` de cerca de 100 KB, sem instalação e sem precisar de administrador.
- **Relatar problema:** em *Sobre* tem um botão que copia o relatório do app e abre a página de problemas do GitHub.

## 🚀 Como usar

1. Baixe o **[`WatermarkRemoverByBrendler.zip`](https://github.com/fbrendler/WatermarkRemover-Brendler/raw/main/download/WatermarkRemoverByBrendler.zip)** e extraia.
2. Dê dois cliques em **`WatermarkRemoverByBrendler.exe`**.
3. Se aparecer *"O Windows protegeu o computador"*, clique em **Mais informações → Executar assim mesmo**. O aviso aparece porque o programa não tem assinatura digital paga.
4. Escolha uma opção:

| Opção | O que faz |
|---|---|
| **Remover a marca d'água agora** | Remove até o próximo reinício do PC. |
| **Remover agora e sempre que o PC ligar** | Remove e passa a rodar sozinho, escondido, a cada login. |
| **Desativar a remoção automática** | Para de rodar ao ligar o PC. |
| **Abrir o relatório (log)** | Mostra o que o programa fez, para diagnóstico. |
| **Sobre** | Versão, relatar problema e página do projeto. |

> Na primeira vez, e depois de cada atualização do Windows, é preciso **internet**: o programa baixa um arquivo de símbolos do servidor oficial da Microsoft.

## 🔧 Como funciona

A marca d'água é desenhada pelo Explorer (`explorer.exe`) através da função
`CDesktopWatermark::s_DesktopBuildPaint`, que fica no `shell32.dll`.

1. O programa lê no `shell32.dll` a identificação exata da sua build do Windows.
2. Baixa os **símbolos públicos** dessa build em `msdl.microsoft.com`, o servidor de símbolos da Microsoft, e descobre onde fica a função.
3. Confere os bytes da função e coloca uma instrução `ret` no início dela, **só na memória** do Explorer em execução. A função passa a não desenhar nada.
4. Atualiza a área de trabalho.

Nenhum arquivo do Windows é tocado. Se o Explorer ou o PC reiniciar, tudo volta ao normal. Por isso existe a opção de remoção automática.

### Arquivos que o programa cria

Todos ficam em `%LOCALAPPDATA%\Brendler\WatermarkRemover`:

- `log.txt`: o relatório.
- `simbolos\`: o cache dos símbolos baixados.
- `WatermarkRemoverByBrendler.exe`: uma cópia do programa, criada só se a remoção automática estiver ativada.

A remoção automática usa a entrada `BrendlerWatermarkRemover` em
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

## 🧹 Desinstalar

1. Abra o programa e clique em **Desativar a remoção automática**.
2. Apague a pasta `%LOCALAPPDATA%\Brendler\WatermarkRemover`.

## ⚠️ Avisos

- Funciona no **Windows 11 x64**. Não funciona em PCs com processador ARM.
- Alguns antivírus podem desconfiar, porque o programa escreve na memória do Explorer. Se isso acontecer, adicione uma exceção.
- Remove **só** a marca d'água das versões Insider/avaliação. **Não** remove o aviso "Ativar o Windows" e não serve para burlar ativação.
- As builds Insider expiram. Mantenha o Windows atualizado.

## 🛠️ Compilar

Requer **MinGW-w64**, no Linux (`sudo apt install gcc-mingw-w64-x86-64`) ou no MSYS2 do Windows:

```sh
cd src
./build.sh
```

## 🙏 Créditos

A ideia de desativar `s_DesktopBuildPaint` na memória veio do projeto
[uwd2](https://github.com/machineonamission/uwd2), sucessor do
[Universal Watermark Disabler](https://github.com/pr701/universal-watermark-disabler).
O código deste projeto foi escrito do zero, em C.

## 🐞 Problemas

Abra uma [issue](../../issues/new). Pelo app, use **Sobre → Relatar problema**: o relatório é copiado automaticamente e você só precisa colar.

---

<p align="center">© 2026 Brendler · Todos os direitos reservados</p>
