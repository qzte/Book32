# Checklist de fumo pré-release

Os testes de host (`tools/tests/test_*.cpp`, ver README) cobrem lógica pura
— hifenização, merge de progresso, paginação, dígitos de OTA, etc. — mas
correm num compilador de PC, nunca no dispositivo real. Este checklist cobre
o que só existe em hardware: arranque, ecrã E-Ink, WiFi, OTA, botões físicos.

Corre-se manualmente num Book32 real antes de cada tag `vX.Y.Z`. Não é um
teste automatizado (ver `docs/plans/2026-08-23-avaliacao-trmnl-firmware.md`,
recomendação #7, sobre porquê uma suite de integração em hardware seria
desproporcionada para um projeto de uma só placa).

## Antes de começar

- [ ] `pio run` compila sem avisos novos face à baseline conhecida
- [ ] Todos os testes de host passam (comando no README, secção
      "Development")
- [ ] Ler o `TODO.txt` da raiz do repo — pode ter verificações específicas
      da versão em preparação, além dos passos genéricos abaixo

## 1. Arranque

- [ ] Ecrã de boot aparece e progride até à biblioteca sem travar
- [ ] Biblioteca abre com os livros já existentes no dispositivo, com os
      títulos corretos (não nomes de ficheiro cortados)
- [ ] Indicador de bateria mostra uma percentagem plausível e o estado de
      carregamento correto (com/sem USB ligado)
- [ ] Se WiFi já estava configurado, liga automaticamente e mostra o IP no
      menu principal

## 2. Upload e leitura de EPUB

- [ ] Upload de um EPUB novo pela web UI conclui sem erro
- [ ] O livro aparece na biblioteca no dispositivo com o título correto
      (incluindo acentos portugueses: Ó, ç, ã)
- [ ] Abrir o livro: KEY3 (click) avança página, KEY1 (click) volta atrás
- [ ] Sair do livro e voltar a abri-lo: retoma na página onde ficou
- [ ] Apagar o livro pela web UI: desaparece da biblioteca no scan seguinte

## 3. OTA

- [ ] A partir da versão anterior instalada, correr a atualização pela web
      UI ou menu do dispositivo; conclui com sucesso
- [ ] Testar o caminho de recusa: apontar a um binário com checksum ou
      assinatura inválida e confirmar que a atualização é rejeitada de forma
      limpa (sem ficar preso a meio, sem corromper a partição corrente)
- [ ] Reiniciar após a OTA: livros, progresso e configurações (WiFi
      incluído) mantêm-se intactos

## 4. Standby / wake

- [ ] Long-press KEY2 entra em standby
- [ ] KEY3 acorda o dispositivo e volta ao ecrã onde estava antes de
      dormir, sem perder o estado

## 5. Verificações pendentes específicas da versão

`TODO.txt` guarda, por versão, os pontos que os testes de host não cobrem
(ex.: qualidade visual do dithering das capas, comportamento da indexação
de TOC em segundo plano). Corre os itens ainda por marcar aí antes de
fechar a tag; risca-os do `TODO.txt` à medida que forem confirmados.

## 6. Teste dedicado: rollback OTA automático do bootloader

Este teste **não é por release** — corre-se uma vez para responder à
pergunta em aberto em
`docs/plans/2026-08-23-post-ota-rollback-design.md`: será que o bootloader
do `framework = arduino` puro reverte mesmo sozinho para a partição
anterior quando uma OTA nova nunca se confirma válida?

`src/main.cpp:67` chama `esp_ota_mark_app_valid_cancel_rollback()` como a
**primeira linha** de `setup()`. Isso significa que, em condições normais,
a app confirma-se válida antes de qualquer código com potencial para
crashar correr — o que é o comportamento correto em produção, mas também
significa que um crash normal do Book32 nunca vai exercitar o rollback:
a essa altura o bootloader já foi avisado que a imagem está boa.

Para testar o mecanismo em si, é preciso simular uma OTA que nunca chega a
confirmar-se:

1. Criar um branch/build só para este teste com essa chamada comentada em
   `src/main.cpp` (ou trocada por um `abort()` imediato antes dela), para
   que o bootloader nunca receba a confirmação de "app válida".
2. Com um Book32 numa versão funcional conhecida, instalar este build por
   **OTA** (não por USB — o mecanismo de rollback é do OTA, um reflash por
   USB reescreve a partição diretamente e não passa por aqui).
3. Observar o monitor série (`pio device monitor`) através de vários ciclos
   de arranque:
   - Se, ao fim de algumas tentativas falhadas, o dispositivo volta sozinho
     a arrancar a versão anterior (visível no log de boot ou na versão
     reportada pela app) → o rollback automático do bootloader **está**
     ativo.
   - Se o dispositivo fica preso a rebentar sempre com a versão nova, sem
     nunca recuperar sozinho → **não** está ativo nesta build, e o único
     caminho de recuperação continua a ser USB
     (`pio run --target upload` + `--target uploadfs`, já documentado no
     README).
4. Registar o resultado em
   `docs/plans/2026-08-23-post-ota-rollback-design.md`, fechando a secção
   "Limites" desse documento com a resposta observada em vez da hipótese
   não verificada.
5. Reflashar o dispositivo de teste com uma versão de produção normal
   (chamada de confirmação incluída) antes de voltar a usá-lo.
