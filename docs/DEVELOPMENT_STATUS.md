# Shadow Run — checkpoint de desenvolvimento

Este checkpoint preserva o protótipo compilado na Unreal Engine 5.8. Os 12 layouts passaram no diagnóstico geométrico com alertas. Nenhum deles está homologado funcionalmente.

## O que está implementado

- Seis módulos de combate com variantes A/B: Depósito, Anel de Máquinas, Cruzamento, Subestação, Drenagem e Galeria Dupla. A geração prepara a sala seguinte, verifica a ocupação e mantém a recepção e a oficina como etapas distintas.
- Metas de células configuráveis por módulo e andar em `DefaultGame.ini`, coleta com E, depósito no terminal e passagem após cumprir a meta. O teste de controles foi adaptado à coleta por interação.
- HUD com painéis de objetivo, energia, integridade e munição; avisos contextuais para coleta, terminal e bancada; indicador de recarga e escala da interface.
- Seleção direcionada da primeira sala de combate e Validador Modular V2, com logs de módulo, variante, alcance por grade e pares de componentes com bounds sobrepostos.

## Evidência já coletada

O build `Shadow_RunEditor Win64 Development` passou na UE 5.8 em 12,50 s. O controle em Drenagem A e os 12 processos da matriz registraram `SHADOW_DEBUG_SELECTION_OK`, `ready=1` e código de saída 0. Todos registraram terminal e aproximação da saída acessíveis pela grade. A seed 11 foi usada para manter a mesma configuração de saída (Y=0); a seleção do layout foi feita pelos parâmetros de diagnóstico.

| Identificador em runtime | Variante | Pontos candidatos de células | Componentes | Alertas | Diagnóstico |
|---|---|---:|---:|---:|---|
| Combat.Depot | A | 3/3 | 67 | 8 | Com alertas |
| Combat.Depot | B | 3/3 | 67 | 8 | Com alertas |
| Combat.MachineRing | A | 3/3 | 70 | 8 | Com alertas |
| Combat.MachineRing | B | 3/3 | 70 | 8 | Com alertas |
| Combat.Crossroads | A | 3/3 | 68 | 8 | Com alertas |
| Combat.Crossroads | B | 3/3 | 68 | 8 | Com alertas |
| Combat.Substation | A | 3/3 | 66 | 8 | Com alertas |
| Combat.Substation | B | 3/3 | 66 | 8 | Com alertas |
| Combat.Drainage | A | 3/3 | 66 | 8 | Com alertas |
| Combat.Drainage | B | 3/3 | 66 | 8 | Com alertas |
| Combat.DoubleGallery | A | 3/3 | 69 | 8 | Com alertas |
| Combat.DoubleGallery | B | 3/3 | 69 | 8 | Com alertas |

Os logs usam o prefixo `SHADOW_MODULE_VALIDATION`. As cópias e logs ficaram em `E:\UNREAL\Shadow_ValidatorV2_20261114`, fora do Git. Toolsets experimentais da Engine emitiram erros de inicialização Python em `-game` depois do diagnóstico, sem interromper o validador.

## Limites e pendências

1. A grade de 32 cm usa cápsula de raio 34 cm e meia altura 88 cm. Testa posições amostradas, sem sweep contínuo entre vizinhos. O resultado cobre pontos candidatos, não coleta real nem travessia da porta aberta. Testar também as outras posições de saída.
2. Os oito pares de bounds se repetiram nas molduras: quatro na entrada, quatro na saída. No controle, parede/batente envolvem `StaticMeshActor_67/72`, `68/74`, `146/151` e `147/153`; os demais pares ficam nas junções com as vergas. A classificação foi `INDETERMINATE`; `affectsPawn=0` é inconclusivo porque a consulta ignora os atores do par. Corrigir a atribuição antes de mover peças.
3. Revalidar com movimento real os bloqueios históricos: Drenagem, local `(470, -300, 52.5)`; Depósito, local `(565, -300, 52.5)`, em centímetros. Os logs históricos não registram a variante. O ponto mundial histórico da Drenagem ficou fora da sala no controle atual; o indicador de alcance desse ponto deve ser desconsiderado.
4. Em PIE, percorrer cada variante com o personagem, coletar com E, depositar, conferir abertura e travessia, bloqueio de retorno, descarregamento da sala anterior e spawns. Reexecutar `ShadowControlsCheck` em cópia isolada depois dos ajustes: a última rodada completa disponível teve 101/102, com falha de cadência da carabina. A matriz geométrica não executou essa suíte.
5. Auditar a tipografia contra [o GDD v2](../SourceAssets/Shadow_Run_GDD_v2.pdf): conferir papéis e aplicação de Barlow Condensed e Chakra Petch, hierarquia, escala, caracteres acentuados e renderização na HUD, menus e build empacotado. Conformidade tipográfica ainda não confirmada.

Próxima etapa recomendada: corrigir as limitações do diagnóstico de colisão e confirmar os dois pontos históricos em PIE; depois homologar os 12 percursos antes de avançar para novos inimigos ou encontros.

## Restaurar em outro computador

1. Clone a branch `main` com acesso à conta autorizada: `git clone --branch main https://github.com/patrickbotarick/Shadow-Run.git`. Assets e fontes já estão no repositório.
2. Instale Unreal Engine 5.8, Visual Studio 2022 ou Build Tools com ferramentas C++ e Windows SDK, e o .NET Framework 4.8 Developer Pack (NetFxSDK). O ambiente deste checkpoint compilou com MSVC 14.44 e Windows SDK 10.0.26100.0.
3. Feche o Editor e, no PowerShell na raiz do clone, ajuste o caminho da instalação se necessário e compile:

   ```powershell
   & 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' Shadow_RunEditor Win64 Development (Join-Path (Get-Location).Path 'Shadow_Run.uproject') -WaitMutex
   ```

4. Abra `Shadow_Run.uproject` e confirme o mapa `/Game/ShadowRun/Maps/L_Industrial`. Para Unreal MCP, confira os plugins ModelContextProtocol e AllToolsets; em Editor Preferences, configure Auto Start, porta 8000, caminho `/mcp` e Tool Search. Aponte o cliente Codex para `http://127.0.0.1:8000/mcp` e confira a descoberta de ferramentas. As preferências locais não vêm do Git.
5. Para diagnóstico, copie o projeto e o binário compilado para uma pasta separada, sem copiar `Saved`. Execute `UnrealEditor.exe` com o caminho do `.uproject` da cópia e estes argumentos:

   ```text
   -game -NullRHI -Unattended -NoSound -ShadowSeed=11 -ShadowValidateModule -ShadowDebugModule=Combat.Drainage -ShadowDebugVariant=A -ExecCmds=quit
   ```

   Troque módulo e variante conforme necessário. Para Depósito, passe `Combat.Cargo`; o log reportará `Combat.Depot`. Confira a seleção, o resumo e os pares em `Saved/Logs/Shadow_Run.log` da cópia.
6. Se precisar levar o progresso pessoal, transfira `Saved/SaveGames/ShadowRun_Profile.sav` separadamente por um canal privado e preserve qualquer save já existente no destino. Builds, caches, saves e o token local gerado pelo Android File Server ficam fora do checkpoint.
